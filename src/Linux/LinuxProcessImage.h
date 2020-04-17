// Copyright (c) 2017-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include <map>
#include "../Allocations/TaggerRunner.h"
#include "../LibcMalloc/FinderGroup.h"
#include "../ProcessImage.h"
#include "../RangeMapper.h"
#include "../Unmangler.h"
#include "ELFImage.h"

namespace chap {
namespace Linux {
template <class ElfImage>
class LinuxProcessImage : public ProcessImage<typename ElfImage::Offset> {
 public:
  typedef typename ElfImage::Offset Offset;
  typedef ProcessImage<Offset> Base;
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef typename AddressMap::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename Allocations::SignatureDirectory<Offset> SignatureDirectory;
  LinuxProcessImage(ElfImage& elfImage, bool truncationCheckOnly)
      : ProcessImage<Offset>(elfImage.GetVirtualAddressMap(),
                             elfImage.GetThreadMap()),
        _elfImage(elfImage),
        _symdefsRead(false) {
    if (_elfImage.GetELFType() != ET_CORE) {
      /*
       * It is the responsibilty of the caller to avoid passing in an ELFImage
       * that corresponds to something other than a core.
       */

      abort();
    }
    if (!truncationCheckOnly) {
      /*
       * Try to find the modules in the quick way.
       */
      bool fastFindModulesWorked = FastFindModules();
      if (fastFindModulesWorked) {
        /*
         * As soon as we know where the modules are, it makes sense to find
         * the python arenas because it is quite fast to find them once
         * the libpython module has been found, and finding and claiming the
         * pages used for the python arenas will make it cheaper to find other
         * things, such as the structures used by libc malloc.
         */
        Base::_pythonFinderGroup.Resolve();
      }

      /*
       * This finds the large structures associated with libc malloc then
       * registers any relevant allocation finders with the allocation
       * directory.
       */

      _libcMallocFinderGroup.reset(new LibcMalloc::FinderGroup<Offset>(
          Base::_virtualMemoryPartition, Base::_moduleDirectory,
          Base::_allocationDirectory, Base::_unfilledImages));

      /*
       * If we haven't yet found the modules, now is a good time to do so
       * because the libc malloc allocation finder does not depend
       * on knowing whether or where any modules are present, and finding
       * libc allocations first considerably shortens the time to find the
       * modules because it allows skipping heaps and main arena allocation
       * runs.
       */
      if (!fastFindModulesWorked) {
        SlowFindModules();
        /*
         * Finding the python infrastructure depends on finding the modules
         * first.
         */
        Base::_pythonFinderGroup.Resolve();
      }

      /*
       * Now that any allocation finders have been registered with the
       * allocaion directory, find out where all the allocations are.
       */
      Base::_allocationDirectory.ResolveAllocationBoundaries();

      /*
       * Static anchor ranges should be found after the allocations and modules,
       * both because both the writable regions for modules and all imaged
       * writable
       * memory is considered to be OK for anchors.  This is sometimes
       * inaccurate,
       * because mmapped memory not allocated by a known allocator is considered
       * as
       * anchors, but it is necessary to consider the unknown regions to be
       * anchors
       * to avoid false leaks.
       */
      FindStaticAnchorRanges();

      Base::_allocationGraph = new Allocations::Graph<Offset>(
          Base::_virtualAddressMap, Base::_allocationDirectory,
          Base::_threadMap, _staticAnchorLimits, nullptr, nullptr);

      /*
       * In Linux processes the current approach is to wait until the
       * allocations have been found, then treat pointers at the start of
       * the allocations to read only memory as signatures.  This means
       * that the signatures can't be identified until the allocations have
       * been found.
       */

      FindSignaturesInAllocations();

      FindSignatureNamesFromBinaries();

      WriteSymreqsFileIfNeeded();

      UpdateStacksAndStackGuards();

      /*
       * Once this constructor as finished, any classification of ranges is
       * done.
       */
      Base::_virtualMemoryPartition.ClaimUnclaimedRangesAsUnknown();

      Base::TagAllocations();
    }
  }

  LibcMalloc::FinderGroup<Offset>& GetLibcMallocFinderGroup() const {
    return *(_libcMallocFinderGroup.get());
  }

  void RefreshSignaturesAndAnchors() {
    if (!_symdefsRead) {
      ReadSymdefsFile();
    }
  }

  template <typename T>
  struct CompareByAddressField {
    bool operator()(const T& left, const T& right) {
      return left._address < right._address;
    }
  };

 private:
  std::unique_ptr<LibcMalloc::FinderGroup<Offset> > _libcMallocFinderGroup;

  /*
   * Stacks that are associated with threads have already been registered at
   * this point, but stack guards for those stacks, which are identified in
   * a somewhat Linux-specific way, as well as idle stacks and their guards,
   * still need to be found.
   */
  void UpdateStacksAndStackGuards() {
    Reader reader(Base::_virtualAddressMap);
    typename AddressMap::const_iterator itMapEnd =
        Base::_virtualAddressMap.end();
    for (typename ThreadMap<Offset>::const_iterator it =
             Base::_threadMap.begin();
         it != Base::_threadMap.end(); ++it) {
      Offset base = it->_stackBase;
      Offset limit = it->_stackLimit;
      Offset lastPageBase = limit - 0x1000;
      Offset guardBase = base - 0x1000;
      Offset sizeWithGuard = limit - guardBase;
      for (Offset check = limit - 3 * sizeof(Offset); check >= lastPageBase;
           check -= sizeof(Offset)) {
        if (reader.ReadOffset(check) == guardBase &&
            reader.ReadOffset(check + sizeof(Offset)) == sizeWithGuard &&
            reader.ReadOffset(check + 2 * sizeof(Offset)) == 0x1000) {
          typename AddressMap::const_iterator itMap =
              Base::_virtualAddressMap.find(guardBase);
          if (itMap != itMapEnd) {
            int permissions =
                (itMap.Flags() & RangeAttributes::PERMISSIONS_MASK);
            if ((permissions & (RangeAttributes::PERMISSIONS_MASK ^
                                RangeAttributes::IS_READABLE)) !=
                RangeAttributes::HAS_KNOWN_PERMISSIONS) {
              std::cerr
                  << "Warning: unexpected permissions found for overflow guard "
                  << "for thread " << std::dec << it->_threadNum << ".\n";
              break;
            }
            if ((permissions & RangeAttributes::IS_READABLE) != 0) {
              /*
               * This has been seen in some cores where the guard region
               * has been improperly marked as read-only, even after having
               * been verified as inaccessible at the time the process was
               * running.  We'll grudgingly accept the core's version of the
               * facts here.
               */
              if (!Base::_virtualMemoryPartition.ClaimRange(
                      guardBase, 0x1000, Base::STACK_OVERFLOW_GUARD, false)) {
                std::cerr
                    << "Warning: unexpected overlap found for overflow guard "
                    << "for thread " << std::dec << it->_threadNum << ".\n";
              }
              break;
            }
          }
          /*
           * If we reach here, the range was mentioned in the core as
           * inaccessible or not mentioned at all.  The expected thing
           * to do when the core is created is to record the inaccessible
           * guard region in a Phdr, but not to bother providing an image.
           * Unfortunately, some versions of gdb stray from this and either
           * don't have a Phdr or waste core space on an image.
           */
          if (!Base::_virtualMemoryPartition.ClaimRange(
                  guardBase, 0x1000, Base::STACK_OVERFLOW_GUARD, false)) {
            std::cerr << "Warning: unexpected overlap found for overflow guard "
                      << "for thread " << std::dec << it->_threadNum << ".\n";
          }
          break;
        }
      }
    }
    /*
     * Now that we have figured out the ranges associated with stacks and
     * guards for threads, and figured out the ranges for pretty much anything
     * that chap knows how to figure out for Linux, figure out any stacks
     * associated with pthreads that have finished.
     */

    std::vector<std::pair<Offset, Offset> > basesAndLengths;
    for (const auto& range :
         Base::_virtualMemoryPartition.GetUnclaimedWritableRangesWithImages()) {
      if (range._size < 0x2000) {
        continue;
      }
      Offset base = range._base;
      Offset limit = range._limit;
      Offset secondPageBase = base + 0x1000;
      Offset guardBase = base - 0x1000;
      Offset sizeWithGuard = limit - guardBase;
      typename AddressMap::const_iterator itMap =
          Base::_virtualAddressMap.find(guardBase);
      bool guardMappedReadOnly = false;
      if (itMap != itMapEnd) {
        int permissions = (itMap.Flags() & RangeAttributes::PERMISSIONS_MASK);
        if ((permissions & (RangeAttributes::PERMISSIONS_MASK ^
                            RangeAttributes::IS_READABLE)) !=
            RangeAttributes::HAS_KNOWN_PERMISSIONS) {
          continue;
        }
        if ((permissions & RangeAttributes::IS_READABLE) != 0) {
          guardMappedReadOnly = true;
        }
      }
      for (Offset check = limit - 3 * sizeof(Offset); check >= secondPageBase;
           check -= sizeof(Offset)) {
        if (reader.ReadOffset(check, 0) == guardBase &&
            reader.ReadOffset(check + sizeof(Offset), 0) == sizeWithGuard &&
            reader.ReadOffset(check + 2 * sizeof(Offset), 0) == 0x1000) {
          /*
           * There are some cores where the guard region
           * has been improperly marked as read-only, even after having
           * been verified as inaccessible at the time the process was
           * running.  We'll grudgingly accept the core's version of the
           * facts but still mark it as a guard.
           */
          if (!Base::_virtualMemoryPartition.ClaimRange(
                  guardBase, 0x1000, Base::STACK_OVERFLOW_GUARD, false)) {
            std::cerr << "Warning: unexpected overlap found for overflow "
                         "guard at 0x"
                      << std::hex << guardBase << " for unused stack.\n";
            break;
          }
          Offset length =
              ((check + 3 * sizeof(Offset) + 0xfff) & ~0xfff) - base;
          basesAndLengths.emplace_back(base, length);
          break;
        }
      }
    }
    for (const auto& baseAndLength : basesAndLengths) {
      Offset base = baseAndLength.first;
      Offset length = baseAndLength.second;
      if (!Base::_virtualMemoryPartition.ClaimRange(base, length, Base::STACK,
                                                    false)) {
        std::cerr << "Warning: unexpected overlap found for idle stack at [0x"
                  << std::hex << base << ", " << (base + length) << ")\n";
      }
    }
  }
  bool CheckChainMember(Offset candidate,
                        typename VirtualAddressMap<Offset>::Reader& reader) {
    Offset BAD = 0xbadbad;
    if (((reader.ReadOffset(candidate, BAD) & 0xfff) != 0) ||
        (reader.ReadOffset(candidate + 5 * sizeof(Offset), BAD) != candidate)) {
      return false;
    }
    Offset current = candidate;
    size_t numChecked = 0;
    for (; numChecked < 1000; ++numChecked) {
      Offset next = reader.ReadOffset(current + 3 * sizeof(Offset), BAD);
      if (next == 0) {
        break;
      }
      if (next == BAD) {
        return false;
      }
      Offset nameStart = reader.ReadOffset(next + sizeof(Offset), BAD);
      if ((nameStart == BAD) || ((reader.ReadOffset(next, BAD) & 0xfff) != 0) ||
          (reader.ReadOffset(next + 5 * sizeof(Offset), BAD) != next) ||
          (reader.ReadOffset(next + 4 * sizeof(Offset), BAD) != current)) {
        return false;
      }
      current = next;
    }
    if (numChecked == 1000) {
      return false;
    }
    current = candidate;
    Offset chainHead = 0;
    for (numChecked = 0; numChecked < 1000; ++numChecked) {
      Offset prev = reader.ReadOffset(current + 4 * sizeof(Offset), BAD);
      if (prev == 0) {
        chainHead = current;
        break;
      }
      if (prev == BAD) {
        return false;
      }
      Offset nameStart = reader.ReadOffset(prev + sizeof(Offset), BAD);
      if ((nameStart == BAD) || ((reader.ReadOffset(prev, BAD) & 0xfff) != 0) ||
          (reader.ReadOffset(prev + 5 * sizeof(Offset), BAD) != prev) ||
          (reader.ReadOffset(prev + 3 * sizeof(Offset), BAD) != current)) {
        return false;
      }
      current = prev;
    }
    if (chainHead == 0) {
      return false;
    }

    size_t chainLength = 0;
    size_t expectedNumVotes = 0;
    Offset maxStructSize = (Offset)(0x1000);
    for (Offset link = chainHead; link != 0;) {
      chainLength++;
      if (reader.ReadOffset(link, 0) != 0) {
        expectedNumVotes++;
      }
      Offset next = reader.ReadOffset(link + 3 * sizeof(Offset), BAD);
      if (link < next) {
        Offset maxSize = (next - link);
        if (maxSize < maxStructSize) {
          maxStructSize = maxSize;
        }
      }
      link = next;
    }

    Offset bestNumVotes = 0;
    Offset bestOffsetOfPair = 0;
    for (Offset offsetOfPair = 64 * sizeof(Offset);
         offsetOfPair < maxStructSize; offsetOfPair += sizeof(Offset)) {
      Offset numVotes = 0;
      for (Offset link = chainHead; link != 0;
           link = reader.ReadOffset(link + 3 * sizeof(Offset), BAD)) {
        Offset firstOffset = reader.ReadOffset(link, 0);
        if ((firstOffset != 0) &&
            (reader.ReadOffset(link + offsetOfPair, 0) == firstOffset)) {
          numVotes++;
        }
      }
      if (numVotes > bestNumVotes) {
        bestNumVotes = numVotes;
        bestOffsetOfPair = offsetOfPair;
        if (bestNumVotes == expectedNumVotes) {
          break;
        }
      }
    }
    if (bestNumVotes == 0) {
      expectedNumVotes = chainLength;
      for (Offset offsetOfPair = 64 * sizeof(Offset);
           offsetOfPair < maxStructSize; offsetOfPair += sizeof(Offset)) {
        Offset numVotes = 0;
        for (Offset link = chainHead; link != 0;
             link = reader.ReadOffset(link + 3 * sizeof(Offset), BAD)) {
          Offset base = reader.ReadOffset(link + offsetOfPair, BAD);
          if ((base & 0xfff) != 0) {
            continue;
          }
          Offset limit =
              reader.ReadOffset(link + offsetOfPair + sizeof(Offset), BAD);
          if (base >= limit) {
            continue;
          }
          typename VirtualAddressMap<Offset>::const_iterator it =
              Base::_virtualAddressMap.find(base);
          if ((it != Base::_virtualAddressMap.end()) &&
              ((it.Flags() & RangeAttributes::IS_WRITABLE) == 0) &&
              (it.Base() == base)) {
            numVotes++;
          }
        }
        if (numVotes > bestNumVotes) {
          bestNumVotes = numVotes;
          bestOffsetOfPair = offsetOfPair;
          if (bestNumVotes == expectedNumVotes) {
            break;
          }
        }
      }
    }
    if (bestNumVotes == 0) {
      std::cerr << "Cannot figure out how to identify module ends.\n";
      return false;
    }
    for (Offset link = chainHead; link != 0;
         link = reader.ReadOffset(link + 3 * sizeof(Offset), BAD)) {
      const char* name = nullptr;
      if (link == chainHead) {
        name = "main executable";
      } else {
        Offset nameStart = reader.ReadOffset(link + sizeof(Offset), BAD);
        if (nameStart == BAD) {
          std::cerr << "Module chain entry at 0x" << std::hex << link
                    << " is not fully mapped.\n";
          continue;
        }
        if (nameStart == 0) {
          std::cerr << "Module chain entry at 0x" << std::hex << link
                    << " has no name pointer.\n";
          continue;
        }
        typename VirtualAddressMap<Offset>::const_iterator it =
            Base::_virtualAddressMap.find(nameStart);
        if (it == Base::_virtualAddressMap.end()) {
          std::cerr << "Module chain entry at 0x" << std::hex << link
                    << " has an invalid name pointer.\n";
          continue;
        }
        if ((it.Flags() & RangeAttributes::IS_MAPPED) == 0) {
          std::cerr << "Module chain entry at 0x" << std::hex << link
                    << " has an unmapped name.\n";
          continue;
        }
        if ((it.Flags() & RangeAttributes::IS_TRUNCATED) != 0) {
          std::cerr << "Module chain entry at 0x" << std::hex << link
                    << " has a name lost to\nprocess image truncation.\n";
          continue;
        }
        Offset numBytesFound =
            Base::_virtualAddressMap.FindMappedMemoryImage(nameStart, &name);
        if (numBytesFound < 2 || name[0] == 0) {
          continue;
        }
      }
      Offset base = reader.ReadOffset(link + bestOffsetOfPair, BAD);
      Offset limit =
          reader.ReadOffset(link + bestOffsetOfPair + sizeof(Offset), BAD);
      if (base == BAD || limit == BAD) {
        std::cerr << "Module chain entry at 0x" << std::hex << link
                  << " is not fully mapped.\n";
        continue;
      }
      if (base == 0 || (base & 0xfff) != 0) {
        std::cerr << "Module chain entry at 0x" << std::hex << link
                  << " has unexpected module base 0x" << base << ".\n";
        continue;
      }
      if (limit < base) {
        std::cerr << "Module chain entry at 0x" << std::hex << link
                  << " has unexpected module limit 0x" << limit << ".\n";
        continue;
      }
      limit = (limit + 0xfff) & ~0xfff;
      Base::_moduleDirectory.AddRange(base, limit - base, name);
    }
    return true;
  }

  bool FindModulesFromELIFNote(std::string& noteName, const char* description,
                               typename ElfImage::ElfWord noteType) {
    if (noteName == "CORE" && noteType == 0x46494c45) {
      Offset numEntries = *((const Offset*)(description));
      const Offset* arrayStart = ((const Offset*)(description)) + 2;
      const char* stringStart =
          description + ((2 + numEntries * 3) * sizeof(Offset));
      const Offset* arrayLimit = arrayStart + 3 * numEntries;
      for (const Offset* entry = arrayStart; entry < arrayLimit; entry += 3) {
        Offset base = entry[0];
        Offset limit = entry[1];
        /*
         * entry[2] is the offset at which the given section would appear in the
         * corresponding shared libary or executable, but we don't bother
         * keeping that
         * information at present, because there is not any use for it yet.
         */
        Offset size = limit - base;
        Base::_moduleDirectory.AddRange(base, size, std::string(stringStart));
        stringStart += strlen(stringStart) + 1;
      }
      return true;
    }
    return false;
  }

  bool PatchOneRegionBasedOnLink(Offset link, Offset pairRelLink,
                                 Reader& reader) {
    Offset base = reader.ReadOffset(link + pairRelLink, 0xbad);
    if ((base & 0xfff) != 0 || base == 0) {
      std::cerr << "Warning. The module chain has an ill formed base 0x"
                << std::hex << base << " for link 0x" << link << ".\n";
      return false;
    }
    Offset moduleRelativeOffset;
    Offset rangeBase = 0;
    Offset rangeSize = 0;
    std::string moduleName;
    if (!Base::_moduleDirectory.Find(base, moduleName, rangeBase, rangeSize,
                                     moduleRelativeOffset)) {
      // For now we ignore any entries in the chain that are not backed by the
      // ELIF.
      return true;
    }
    Offset newLimit =
        reader.ReadOffset(link + pairRelLink + sizeof(Offset), 0xbad);
    if (newLimit == 0xbad) {
      std::cerr << "Warning. The module chain has a truncated link 0x" << link
                << ".\n";
      return false;
    }
    if (!Base::_moduleDirectory.ExtendLastRange(base,
                                                (newLimit + 0xfff) & ~0xfff)) {
      std::cerr << "Warning: Failed to update module directory based on link 0x"
                << std::hex << link << ".\n";
      return false;
    }
    return true;
  }
  void PatchLastModuleRegionsBasedOnChain(Offset chainStart,
                                          Offset pairRelLink) {
    bool errorsInChain = false;
    Reader reader(Base::_virtualAddressMap);
    Offset link = chainStart;
    for (Offset link = chainStart; link != 0 && link != 0xbad;
         link = reader.ReadOffset(link + 4 * sizeof(Offset), 0xbad)) {
      if (!PatchOneRegionBasedOnLink(link, pairRelLink, reader)) {
        errorsInChain = true;
      }
    }
    if (link == 0xbad) {
      std::cerr << "Warning: The module chain is ill formed.\n";
    }
    if (link == 0xbad || errorsInChain) {
      std::cerr << "Warning: Some trailing regions may be truncated.\n";
    }
  }
  /*
   * Try to find modules by finding an entry in the PT_NOTE section that
   * is of type 0x46494c45 and reading the file paths and range boundaries
   * from that entry.  This is the favored from when it works, because it
   * provides resolved paths, whereas other forms often base path names on
   * a referenced path, which may be a symbolic link on the system where
   * the core was generated and may not be resolvable if chap is run
   * elsewhere.
   */

  bool FindModulesByPTNote() {
    if (_elfImage.VisitNotes(
            std::bind(&LinuxProcessImage<ElfImage>::FindModulesFromELIFNote,
                      this, std::placeholders::_1, std::placeholders::_2,
                      std::placeholders::_3))) {
      /*
       * We know where all the modules reside but it is unfortunately not
       * uncommon
       * for the ranges with the .bss for each module to have a limit that
       * is too
       * short.  This allows it to be corrected relatively cheaply.
       */
      for (const auto& moduleNameAndRanges : Base::_moduleDirectory) {
        if (moduleNameAndRanges.first.find("/ld") == std::string::npos) {
          continue;
        }
        auto itEnd = moduleNameAndRanges.second.end();
        auto it = moduleNameAndRanges.second.begin();
        if (it == itEnd) {
          continue;
        }
        Offset loaderBase = it->_base;
        while (++it != itEnd) {
          auto itMap = Base::_virtualAddressMap.find(it->_base);
          if (itMap == Base::_virtualAddressMap.end()) {
            continue;
          }
          if ((itMap.Flags() & RangeAttributes::IS_WRITABLE) !=
              RangeAttributes::IS_WRITABLE) {
            continue;
          }
          const char* image = itMap.GetImage();
          if (image == nullptr) {
            continue;
          }
          Offset base = itMap.Base();
          Offset firstRefToLoaderBase = 0;
          Offset secondRefToLoaderBase = 0;
          const char* imageLimit = image + (itMap.Limit() - base);
          for (const char* candidate = image; candidate < imageLimit;
               candidate += sizeof(Offset)) {
            if (*((Offset*)(candidate)) == loaderBase) {
              Offset ref = base + (candidate - image);
              if (firstRefToLoaderBase == 0) {
                firstRefToLoaderBase = ref;
              } else {
                if (ref - firstRefToLoaderBase > 5 * sizeof(Offset) &&
                    ((Offset*)(image + (firstRefToLoaderBase - base)))[5] ==
                        firstRefToLoaderBase) {
                  secondRefToLoaderBase = ref;
                  PatchLastModuleRegionsBasedOnChain(
                      firstRefToLoaderBase, ref - firstRefToLoaderBase);
                  break;
                } else {
                  firstRefToLoaderBase = ref;
                }
              }
            }
          }
        }
      }
      return true;
    }
    return false;
  }

  /*
   * Try to find a loader chain with the guess that at least one member has
   * the expected alignment, which is assumed but not checked to be a power
   * of 2.
   */
  bool FindModulesByAlignedLink(Offset expectedAlignment) {
    Reader reader(Base::_virtualAddressMap);
    for (const auto& range :
         Base::_virtualMemoryPartition.GetUnclaimedWritableRangesWithImages()) {
      Offset base = range._base;
      Offset align = base & (expectedAlignment - 1);
      if ((align) != 0) {
        base += (expectedAlignment - align);
      }
      Offset limit = range._limit - 0x2f;
      for (Offset candidate = base; candidate <= limit;
           candidate += expectedAlignment) {
        if (CheckChainMember(candidate, reader)) {
          return true;
        }
      }
    }
    return false;
  }
  void FindModulesByMappedImages() {
    Offset executableAddress = 0;
    typename AddressMap::const_iterator itEnd = Base::_virtualAddressMap.end();
    for (typename AddressMap::const_iterator it =
             Base::_virtualAddressMap.begin();
         it != itEnd; ++it) {
      int flags = it.Flags();
      const char* image = it.GetImage();
      Offset base = it.Base();
      Offset limit = it.Limit();
      if ((flags & RangeAttributes::PERMISSIONS_MASK) !=
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_EXECUTABLE |
           RangeAttributes::HAS_KNOWN_PERMISSIONS)) {
        continue;
      }
      if (image == 0) {
        continue;
      }
      if (strncmp((const char*)image, ELFMAG, SELFMAG)) {
        continue;
      }

      const unsigned char elfClass = ((const unsigned char*)image)[EI_CLASS];
      bool acceptElfClass = true;
      if (elfClass == ELFCLASS64) {
        const Elf64_Ehdr* elfHeader = (const Elf64_Ehdr*)(image);
        if (elfHeader->e_type == ET_EXEC) {
          if (executableAddress == 0) {
            executableAddress = base;
          } else {
            std::cerr << "An image of an ELF executable was found at both 0x"
                      << std::hex << executableAddress << " and 0x" << base
                      << ".\n";
            std::cerr
                << "This is unexpected but probably won't break anything.\n";
          }
        } else if (elfHeader->e_type != ET_DYN) {
          continue;
        }
        if (sizeof(Offset) != 8) {
          std::cerr << "Image of 64 bit library or executable currently not "
                       "supported with 32 bit core.\n";
          acceptElfClass = false;
        }
      } else if (elfClass == ELFCLASS32) {
        const Elf32_Ehdr* elfHeader = (const Elf32_Ehdr*)(image);
        if (elfHeader->e_type == ET_EXEC) {
          if (executableAddress == 0) {
            executableAddress = base;
          } else {
            std::cerr << "An image of an ELF executable was found at both 0x"
                      << std::hex << executableAddress << " and 0x" << base
                      << ".\n";
            std::cerr
                << "This is unexpected but probably won't break anything.\n";
          }
        } else if (elfHeader->e_type != ET_DYN) {
          continue;
        }
        if (sizeof(Offset) != 4) {
          std::cerr << "Image of 32 bit library or executable currently not "
                       "supported with 64 bit core.\n";
          acceptElfClass = false;
        }
      } else {
        std::cerr << "Elf class " << std::dec << elfClass
                  << " will not be included in module directory.\n";
        acceptElfClass = false;
      }
      if (!acceptElfClass) {
        std::cerr << "Image at 0x" << std::hex << base
                  << " will be excluded from module directory.\n";
        continue;
      }

      const char* name = nullptr;
      if (executableAddress == base) {
        name = "main executable";
      }
      Offset dynStrAddr = 0;
      Offset nameInDynStr = 0;
      if (elfClass == ELFCLASS64) {
        const Elf64_Ehdr* elfHeader = (const Elf64_Ehdr*)(image);
        int entrySize = elfHeader->e_phentsize;
        Offset minimumExpectedRegionSize =
            elfHeader->e_phoff + (elfHeader->e_phnum * entrySize);
        const char* headerImage = image + elfHeader->e_phoff;
        const char* headerLimit = image + minimumExpectedRegionSize;
        if (it.Size() < minimumExpectedRegionSize) {
          std::cerr << "Contiguous image of module at 0x" << std::hex << base
                    << " is only " << it.Size() << " bytes.\n";
          continue;
        }

        bool firstPTLoadFound = false;
        bool adjustByBase = false;
        Offset dynBase = 0;
        Offset dynSize = 0;
        for (; headerImage < headerLimit; headerImage += entrySize) {
          Elf64_Phdr* programHeader = (Elf64_Phdr*)(headerImage);
          Offset vAddr = programHeader->p_vaddr;
          if (programHeader->p_type == PT_LOAD) {
            if (!firstPTLoadFound) {
              firstPTLoadFound = true;
              adjustByBase = (vAddr == 0);
            }
            if (adjustByBase) {
              vAddr += base;
            }
            Offset loadLimit =
                ((vAddr + programHeader->p_memsz) + 0xfff) & ~0xfff;
            if (limit < loadLimit) {
              limit = loadLimit;
            }
          } else if (programHeader->p_type == PT_DYNAMIC &&
                     base != executableAddress) {
            /*
             * Defer the rest of the processing, based on the notion that
             * we may not have seen the first PT_LOAD yet and don't know
             * whether to adjust the base.
             */
            dynBase = vAddr;
            dynSize = programHeader->p_memsz;
          }
        }
        if (dynBase != 0) {
          if (adjustByBase) {
            dynBase += base;
          }
          const char* dynImage = 0;
          Offset numBytesFound = Base::_virtualAddressMap.FindMappedMemoryImage(
              dynBase, &dynImage);
          if (numBytesFound < dynSize) {
#if 0
            // It is a regrettably common case that the last thing that
            // looks like the image of a shared library refers to a
            // PT_DYNAMIC section that is not actually in the core.
            // For now, don't complain because it happens pretty much
            // for every core.
            std::cerr << "Only 0x" << std::hex << numBytesFound
                      << " bytes found for PT_DYNAMIC section at 0x"
                      << dynBase << "\n... for image at 0x" << base << "\n";
#endif
            continue;
          }
          int numDyn = dynSize / sizeof(Elf64_Dyn);
          const Elf64_Dyn* dyn = (Elf64_Dyn*)(dynImage);
          const Elf64_Dyn* dynLimit = dyn + numDyn;
          for (; dyn < dynLimit; dyn++) {
            if (dyn->d_tag == DT_STRTAB) {
              dynStrAddr = (Offset)dyn->d_un.d_ptr;
            } else if (dyn->d_tag == DT_SONAME) {
              nameInDynStr = (Offset)dyn->d_un.d_ptr;
            }
          }
        } else {
          if (base != executableAddress) {
            std::cerr << "Library image at 0x" << std::hex << base
                      << " has no PT_DYNAMIC section.\n";
          }
        }
      } else {
        const Elf32_Ehdr* elfHeader = (const Elf32_Ehdr*)(image);
        int entrySize = elfHeader->e_phentsize;
        Offset minimumExpectedRegionSize =
            elfHeader->e_phoff + (elfHeader->e_phnum * entrySize);
        const char* headerImage = image + elfHeader->e_phoff;
        const char* headerLimit = image + minimumExpectedRegionSize;
        if (it.Size() < minimumExpectedRegionSize) {
          std::cerr << "Contiguous image of module at 0x" << std::hex << base
                    << " is only " << it.Size() << " bytes.\n";
          continue;
        }

        bool firstPTLoadFound = false;
        bool adjustByBase = false;
        Offset dynBase = 0;
        Offset dynSize = 0;
        for (; headerImage < headerLimit; headerImage += entrySize) {
          Elf32_Phdr* programHeader = (Elf32_Phdr*)(headerImage);
          Offset vAddr = programHeader->p_vaddr;
          if (programHeader->p_type == PT_LOAD) {
            if (!firstPTLoadFound) {
              firstPTLoadFound = true;
              adjustByBase = (vAddr == 0);
            }
            if (adjustByBase) {
              vAddr += base;
            }
            Offset loadLimit =
                ((vAddr + programHeader->p_memsz) + 0xfff) & ~0xfff;
            if (limit < loadLimit) {
              limit = loadLimit;
            }
          } else if (programHeader->p_type == PT_DYNAMIC &&
                     base != executableAddress) {
            /*
             * Defer the rest of the processing, based on the notion that
             * we may not have seen the first PT_LOAD yet and don't know
             * whether to adjust the base.
             */
            dynBase = vAddr;
            dynSize = programHeader->p_memsz;
          }
        }
        if (dynBase != 0) {
          if (adjustByBase) {
            dynBase += base;
          }
          const char* dynImage = 0;
          Offset numBytesFound = Base::_virtualAddressMap.FindMappedMemoryImage(
              dynBase, &dynImage);
          if (numBytesFound < dynSize) {
#if 0
            // It is a regrettably common case that the last thing that
            // looks like the image of a shared library refers to a
            // PT_DYNAMIC section that is not actually in the core.
            // For now, don't complain because it happens pretty much
            // for every core.
            std::cerr << "Only 0x" << std::hex << numBytesFound
                      << " bytes found for PT_DYNAMIC section at 0x"
                      << dynBase << "\n... for image at 0x" << base << "\n";
#endif
            continue;
          }
          int numDyn = dynSize / sizeof(Elf32_Dyn);
          const Elf32_Dyn* dyn = (Elf32_Dyn*)(dynImage);
          const Elf32_Dyn* dynLimit = dyn + numDyn;
          for (; dyn < dynLimit; dyn++) {
            if (dyn->d_tag == DT_STRTAB) {
              dynStrAddr = (Offset)dyn->d_un.d_ptr;
            } else if (dyn->d_tag == DT_SONAME) {
              nameInDynStr = (Offset)dyn->d_un.d_ptr;
            }
          }
        } else {
          if (base != executableAddress) {
            std::cerr << "Library image at 0x" << std::hex << base
                      << " has no PT_DYNAMIC section.\n";
          }
        }
      }
      if (name == nullptr) {
        if (dynStrAddr != 0 && nameInDynStr != 0) {
          Offset numBytesFound = Base::_virtualAddressMap.FindMappedMemoryImage(
              dynStrAddr + nameInDynStr, &name);
          if (numBytesFound < 2) {
            name = nullptr;
          }
        }
      }
      if (name == nullptr) {
#if 0
      // This happens for the last image seen in pretty much every core.
      // It also happens for libraries for which the PT_DYNAMIC section
      // does not contain a DT_SONAME image.  This should be fixed at some
      // point but for now it is not a big deal if we can't identify some
      // of the modules.
      std::cerr << "Unable to find name of module at 0x" << std::hex << base
                << "\n";
#endif
      } else {
        Base::_moduleDirectory.AddRange(base, limit - base, name);
      }
    }
  }
  struct ModuleRange {
    ModuleRange() : _base(0), _size(0), _permissions(0), _image(0) {}
    ModuleRange(Offset base, Offset size, int permissions, const char* image)
        : _base(base), _size(size), _permissions(permissions), _image(image) {}
    ModuleRange(const ModuleRange& other) {
      _base = other._base;
      _size = other._size;
      _permissions = other._permissions;
      _image = other._image;
    }
    Offset _base;
    Offset _size;
    int _permissions;
    const char* _image;
  };
  void FindRangesForModule(typename ModuleDirectory<Offset>::const_iterator it,
                           std::vector<ModuleRange>& ranges) {
    typename ModuleDirectory<Offset>::RangeToFlags::const_iterator itRange =
        it->second.begin();
    const auto& itRangeEnd = it->second.end();
    Offset base = itRange->_base;
    Offset rangeLimit = itRange->_limit;
    typename VirtualAddressMap<Offset>::const_iterator itVirt =
        Base::_virtualAddressMap.find(base);
    typename VirtualAddressMap<Offset>::const_iterator itVirtEnd =
        Base::_virtualAddressMap.end();
    if (itVirt == itVirtEnd) {
      return;
    }
    if (base < itVirt.Base()) {
      base = itVirt.Base();
    }
    Offset virtLimit = itVirt.Limit();
    int permissionsBits = itVirt.Flags() & RangeAttributes::PERMISSIONS_MASK;

    while (true) {
      Offset limit = virtLimit;
      if (limit > rangeLimit) {
        limit = rangeLimit;
      }
      const char* image = itVirt.GetImage();
      if (image != nullptr) {
        image += (base - itVirt.Base());
      }
      ranges.emplace_back(base, limit - base, permissionsBits, image);
      base = limit;
      while (base >= rangeLimit) {
        if (++itRange == itRangeEnd) {
          return;
        }

        if (base < itRange->_base) {
          base = itRange->_base;
        }
        rangeLimit = itRange->_limit;
      }
      while (base >= virtLimit) {
        if (++itVirt == itVirtEnd) {
          return;
        }
        Offset virtBase = itVirt.Base();
        virtLimit = itVirt.Limit();
        if (base < virtBase) {
          base = virtBase;
        }

        permissionsBits = itVirt.Flags() & RangeAttributes::PERMISSIONS_MASK;
      }
    }
  }
  void ClaimRangesForModule(
      typename ModuleDirectory<Offset>::const_iterator it) {
    std::vector<ModuleRange> ranges;
    FindRangesForModule(it, ranges);
    size_t numRanges = ranges.size();
    bool gapFound = false;
    for (const auto& range : ranges) {
      if ((range._permissions & (RangeAttributes::HAS_KNOWN_PERMISSIONS |
                                 RangeAttributes::IS_WRITABLE)) ==
          (RangeAttributes::HAS_KNOWN_PERMISSIONS |
           RangeAttributes::IS_WRITABLE)) {
        if (!Base::_virtualMemoryPartition.ClaimRange(
                range._base, range._size, Base::_moduleDirectory.USED_BY_MODULE,
                true)) {
          std::cerr << "Warning: unexpected overlap found for [0x" << std::hex
                    << range._base << ", 0x" << (range._base + range._size)
                    << ")\nused by module " << it->first << "\n";
        }

      } else if (range._permissions == (RangeAttributes::HAS_KNOWN_PERMISSIONS |
                                        RangeAttributes::IS_READABLE |
                                        RangeAttributes::IS_EXECUTABLE)) {
        if (!Base::_virtualMemoryPartition.ClaimRange(
                range._base, range._size, Base::_moduleDirectory.USED_BY_MODULE,
                true)) {
          std::cerr << "Warning: unexpected overlap found for [0x" << std::hex
                    << range._base << ", 0x" << (range._base + range._size)
                    << ")\nused by module " << it->first << "\n";
        }
      } else if (range._permissions == RangeAttributes::HAS_KNOWN_PERMISSIONS) {
        gapFound = true;
        if (!Base::_virtualMemoryPartition.ClaimRange(
                range._base, range._size,
                Base::_moduleDirectory.MODULE_ALIGNMENT_GAP, false)) {
          std::cerr << "Warning: unexpected overlap found for [0x" << std::hex
                    << range._base << ", 0x" << (range._base + range._size)
                    << ")\nalignment gap for module " << it->first << "\n";
        }
      }
    }
    for (size_t i = 0; i < numRanges; i++) {
      const ModuleRange& range = ranges[i];
      Offset size = range._size;
      if (range._permissions == (RangeAttributes::HAS_KNOWN_PERMISSIONS |
                                 RangeAttributes::IS_READABLE)) {
        if (!gapFound && ((size == 0x200000) || (size == 0x1ff000)) && i > 0 &&
            i < numRanges - 1 && ranges[i + 1]._base == range._base + size) {
          bool knownZeroImage = false;
          if (range._image != nullptr) {
            const char* limit = range._image + range._size;
            knownZeroImage = true;
            for (const char* pC = range._image; pC != limit; ++pC) {
              if (*pC != '\000') {
                knownZeroImage = false;
                break;
              }
            }
          }
          if (knownZeroImage) {
            /*
             * Some versions of gdb incorrectly record inaccessible regions as
             * being readonly and store 0-filled images of such ranges, which
             * makes the core a bit larger.  We do this claiming as a way
             * for the user to understand how the given range is used, but
             * leave it as considered read-only to be consistent with how
             * the range is marked in the core.
             */
            if (!Base::_virtualMemoryPartition.ClaimRange(
                    range._base, range._size,
                    Base::_moduleDirectory.MODULE_ALIGNMENT_GAP, false)) {
              std::cerr << "Warning: unexpected overlap found for [0x"
                        << std::hex << range._base << ", 0x"
                        << (range._base + range._size)
                        << ")\nalignment gap for module " << it->first << "\n";
            }
            gapFound = true;
            continue;
          }
        }
        if (!Base::_virtualMemoryPartition.ClaimRange(
                range._base, range._size, Base::_moduleDirectory.USED_BY_MODULE,
                true)) {
          std::cerr << "Warning: unexpected overlap found for [0x" << std::hex
                    << range._base << ", 0x" << (range._base + range._size)
                    << ")\nused by module " << it->first << "\n";
        }
      }
    }
    /*
     * The gap wasn't found.  Perhaps it wasn't reported for the module and
     * wasn't
     * mapped in memory.
     */
    if (!gapFound && numRanges > 1) {
      for (size_t i = 1; i < numRanges; i++) {
        Offset gapBase = ranges[i - 1]._base + ranges[i - 1]._size;
        Offset gapLimit = ranges[i]._base;
        Offset gapSize = gapLimit - gapBase;
        if (gapSize == 0x200000 || gapSize == 0x1ff000) {
          /*
           * Guess that the gap has been found but we must still make sure that
           * it is legitimately a gap.
           */
          gapFound = true;

          typename VirtualAddressMap<Offset>::const_iterator itVirt =
              Base::_virtualAddressMap.lower_bound(gapBase);
          typename VirtualAddressMap<Offset>::const_iterator itVirtEnd =
              Base::_virtualAddressMap.end();
          for (; itVirt != itVirtEnd && itVirt.Base() < gapLimit; ++itVirt) {
            if ((itVirt.Flags() & RangeAttributes::PERMISSIONS_MASK) !=
                RangeAttributes::HAS_KNOWN_PERMISSIONS) {
              gapFound = false;
              break;
            }
          }
          if (gapFound) {
            if (!Base::_virtualMemoryPartition.ClaimRange(
                    gapBase, gapSize,
                    Base::_moduleDirectory.MODULE_ALIGNMENT_GAP, false)) {
              std::cerr
                  << "Warning: unexpected overlap found for alignment gap [0x"
                  << std::hex << gapBase << ", 0x" << gapLimit
                  << ")\nfor module " << it->first << "\n";
            }
            break;
          }
        }
      }
    }
  }

  void ResolveModuleAlignmentGapsAndModuleDirectory() {
    for (typename ModuleDirectory<Offset>::const_iterator it =
             Base::_moduleDirectory.begin();
         it != Base::_moduleDirectory.end(); ++it) {
      ClaimRangesForModule(it);
    }
    Base::_moduleDirectory.Resolve();
  }

  /*
   * Find the modules in a way that is independent of the size of the core.
   * This should be done very early in processing the core because finding
   * other objects (for example, libc malloc arenas in the single threaded
   * case) may be more efficient if the modules have been found.  This
   * generally works except for with cores generated by rather old versions
   * of gdb.
   */
  bool FastFindModules() {
    bool foundModules = FindModulesByPTNote();
    if (foundModules) {
      ResolveModuleAlignmentGapsAndModuleDirectory();
    }
    return foundModules;
  }

  /*
   * Find the modules in progressively less efficient ways.  These are
   * sufficiently costly that it is best to do this after other large regions
   * have been recognized and registered, so that those regions can be skipped
   * in the scan.
   */
  void SlowFindModules() {
    if (!FindModulesByAlignedLink(0x1000) &&
        !FindModulesByAlignedLink(sizeof(Offset))) {
      FindModulesByMappedImages();
    }
    ResolveModuleAlignmentGapsAndModuleDirectory();
  }

 private:
  ElfImage& _elfImage;
  bool _symdefsRead;
  std::map<Offset, Offset> _staticAnchorLimits;

  bool ParseOffset(const std::string& s, Offset& value) const {
    if (!s.empty()) {
      std::istringstream is(s);
      uint64_t v;
      is >> std::hex >> v;
      if (!is.fail() && is.eof()) {
        value = v;
        return true;
      }
    }
    return false;
  }
  std::string CopyAndUnmangle(
      const VirtualAddressMap<Offset>& virtualAddressMap,
      Offset mangledNameAddr) const {
    std::string unmangledName;
    typename VirtualAddressMap<Offset>::const_iterator it =
        virtualAddressMap.find(mangledNameAddr);
    if (it != virtualAddressMap.end()) {
      char buffer[1000];
      buffer[sizeof(buffer) - 1] = '\000';
      size_t numToCopy = sizeof(buffer) - 1;
      const char* image = it.GetImage();
      if (image != nullptr) {
        Offset maxToCopy = it.Limit() - mangledNameAddr - 1;
        if (numToCopy > maxToCopy) {
          numToCopy = maxToCopy;
        }
        memcpy(buffer, image + (mangledNameAddr - it.Base()), numToCopy);

        Unmangler<Offset> unmangler(buffer, false);
        unmangledName = unmangler.Unmangled();
      }
    }
    return unmangledName;
  }

  std::string GetUnmangledTypeinfoName(
      const VirtualAddressMap<Offset>& virtualAddressMap,
      Offset signature) const {
    std::string emptySignatureName;
    Offset typeInfoPointerAddress = signature - sizeof(Offset);

    Reader reader(virtualAddressMap);
    Offset typeInfoAddress = reader.ReadOffset(typeInfoPointerAddress, 0);
    if (typeInfoAddress == 0) {
      return emptySignatureName;
    }
    Offset typeInfoNameAddress =
        reader.ReadOffset(typeInfoAddress + sizeof(Offset), 0);
    if (typeInfoNameAddress != 0) {
      typename VirtualAddressMap<Offset>::const_iterator it =
          Base::_virtualAddressMap.find(typeInfoNameAddress);
      if ((it != Base::_virtualAddressMap.end()) &&
          ((it.Flags() & RangeAttributes::IS_WRITABLE) == 0)) {
        return CopyAndUnmangle(virtualAddressMap, typeInfoNameAddress);
      }
    }
    return emptySignatureName;
  }

  bool ReadSymdefsFile() {
    std::string symDefsPath(
        Base::_virtualAddressMap.GetFileImage().GetFileName());
    symDefsPath.append(".symdefs");
    std::ifstream symDefs;
    symDefs.open(symDefsPath.c_str());
    if (symDefs.fail()) {
      return false;
    }
    std::string line;
    Offset signature = 0;
    Offset anchor = 0;

    while (getline(symDefs, line, '\n')) {
      size_t lastNonBlank = line.find_last_not_of(' ');
      if (lastNonBlank == std::string::npos) {
        continue;
      }
      if (lastNonBlank != line.size() - 1) {
        line.erase(lastNonBlank + 1);
      }

      if (line.find("SIGNATURE ") == 0) {
        std::string signatureString(line, 10);
        if (!ParseOffset(signatureString, signature)) {
          std::cerr << "\"" << signatureString
                    << "\" is not a valid hexadecimal number\"" << std::endl;
          signature = 0;
        }
        continue;
      }
      if (line.find("ANCHOR ") == 0) {
        std::string anchorString(line, 7);
        if (!ParseOffset(anchorString, anchor)) {
          std::cerr << "\"" << anchorString
                    << "\" is not a valid hexadecimal number\"" << std::endl;
          anchor = 0;
        }
        continue;
      }
      if (line.find("No symbol matches") != std::string::npos || line.empty()) {
        if (signature != 0) {
          Base::_signatureDirectory.MapSignatureNameAndStatus(
              signature, "",
              SignatureDirectory::UNWRITABLE_MISSING_FROM_SYMDEFS);
          signature = 0;
        }
        anchor = 0;
      }

      if (signature != 0) {
        size_t defStart = 0;
        size_t forPos = line.find(" for ");
        bool isVTable = (forPos != std::string::npos);
        if (isVTable) {
          defStart = forPos + 5;
        }
        size_t defEnd = line.find(" in section");
        size_t plusPos = line.find(" + ");
        defEnd = plusPos;
        std::string name(line.substr(defStart, defEnd - defStart));

        /*
         * Convert any, ", " in the demangled signature name to ",", so that
         * one can use the name in scripts.  The issue is embedded blanks.
         * There are still some cases, as with "unsigned long" embedded in
         * a demangled signature that are not resolved this way, but it seems
         * that stripping the space after a comma still leaves the name
         * pretty readable.
         */

        std::string::size_type commaBlankPos;
        while (std::string::npos != (commaBlankPos = name.rfind(", "))) {
          name.erase(commaBlankPos + 1, 1);
        }
        Base::_signatureDirectory.MapSignatureNameAndStatus(
            signature, name,
            isVTable ? SignatureDirectory::VTABLE_WITH_NAME_FROM_SYMDEFS
                     : SignatureDirectory::UNWRITABLE_WITH_NAME_FROM_SYMDEFS);
        signature = 0;
      } else if (anchor != 0) {
        size_t defEnd = line.find(" in section");
        std::string name(line.substr(0, defEnd));
        Base::_anchorDirectory.MapAnchorToName(anchor, name);
        anchor = 0;
      }
    }
    symDefs.close();
    _symdefsRead = true;
    return true;
  }

  /*
   * Initialize the signature directory to contain an entry for each
   * read-only address seen in the pointer at the start of each allocation
   * that is aligned on a pointer-sized boundary.
   */

  void FindSignaturesInAllocations() {
    const Allocations::Directory<Offset>& directory =
        Base::_allocationDirectory;
    typename Allocations::Directory<Offset>::AllocationIndex numAllocations =
        directory.NumAllocations();
    Reader reader(Base::_virtualAddressMap);
    typename VirtualAddressMap<Offset>::const_iterator itEnd =
        Base::_virtualAddressMap.end();
    for (typename Allocations::Directory<Offset>::AllocationIndex i = 0;
         i < numAllocations; ++i) {
      const typename Allocations::Directory<Offset>::Allocation* allocation =
          directory.AllocationAt(i);
      if (!allocation->IsUsed() || (allocation->Size() < sizeof(Offset))) {
        continue;
      }
      Offset signature = reader.ReadOffset(allocation->Address());
      if (Base::_signatureDirectory.IsMapped(signature)) {
        continue;
      }
      if (((signature & (sizeof(Offset) - 1)) != 0) || (signature == 0)) {
        continue;
      }

      typename VirtualAddressMap<Offset>::const_iterator it =
          Base::_virtualAddressMap.find(signature);
      if (it == itEnd) {
        continue;
      }

      bool writableVtable = false;
      typename Allocations::SignatureDirectory<Offset>::Status status =
          SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS;
      if ((it.Flags() & RangeAttributes::IS_WRITABLE) != 0) {
        /*
         * Some recent linkers end up causing vtables to be writable
         * at times.  This is a security bug, but we want chap to
         * support such signatures.  For now they are supported only
         * if the mangled name is actually in the core.  In the case that
         * the vtable is writable, it may be in the static area associated
         * with a module or if not it will be in an area of memory that is not
         * yet analyzed by chap.
         */
        if (Base::_virtualMemoryPartition.IsClaimed(signature)) {
          Offset relativeSignature;
          Offset rangeBase = 0;
          Offset rangeSize = 0;
          std::string newModulePath;
          if (!Base::_moduleDirectory.Find(signature, newModulePath, rangeBase,
                                           rangeSize, relativeSignature)) {
            /*
             * If the signature points to a claimed region, we expect it to
             * refer to a module, as opposed to, for example, dynamically
             * allocated memory.
             */
            continue;
          }
          Offset typeinfoAddr =
              reader.ReadOffset(signature - sizeof(Offset), 0xbadbad);
          if (typeinfoAddr == 0xbadbad) {
            /*
             * If the typeinfo is not in the process image, perhaps the
             * signature does not point to a vtable.  At any rate, excluding
             * this case is needed to avoid false signatures.
             */
            continue;
          }
          Offset toVtableStart =
              reader.ReadOffset(signature - 2 * sizeof(Offset), 0xbadbad);
          if (toVtableStart != 0 &&
              (toVtableStart >= 0x10000 ||
               reader.ReadOffset(signature - 2 * sizeof(Offset) - toVtableStart,
                                 0xbadbad) != 0)) {
            /*
             * Just before the pointer to the typeinfo there should be an offset
             * from that location to the start of the vtable, which always has
             * a 0.
             */
            continue;
          }
          if (!Base::_moduleDirectory.Find(typeinfoAddr, newModulePath,
                                           rangeBase, rangeSize,
                                           relativeSignature)) {
            /*
             * Again to avoid false signatures in this case, we insist that the
             * typeinfo is associated with a module.
             */
            continue;
          }
          status = SignatureDirectory::WRITABLE_MODULE_REFERENCE;
        }
        writableVtable = true;
      }

      std::string typeinfoName =
          GetUnmangledTypeinfoName(Base::_virtualAddressMap, signature);
      if (writableVtable) {
        if (typeinfoName.empty()) {
          /*
           * We were guessing that this was possibly a writable vtable
           * pointer, but didn't actually reach a mangled type name.
           */
          if (status != SignatureDirectory::WRITABLE_MODULE_REFERENCE) {
            /*
             * In the case that both the signature and the possible typeinfo
             * pointers were to modules, we should be willing to try for
             * this as a signature via symreqs/symdefs.  If not, give up.
             */
            continue;
          }
        } else {
          std::cerr << "Warning: type " << typeinfoName
                    << " has a writable vtable at 0x" << std::hex << signature
                    << ".\n";
          std::cerr << "... This is a security violation.\n";
          status =
              SignatureDirectory::WRITABLE_VTABLE_WITH_NAME_FROM_PROCESS_IMAGE;
        }
      } else {
        if (!typeinfoName.empty()) {
          status = SignatureDirectory::VTABLE_WITH_NAME_FROM_PROCESS_IMAGE;
        }
      }
      Base::_signatureDirectory.MapSignatureNameAndStatus(signature,
                                                          typeinfoName, status);
    }
  }

  void FindSignatureNamesFromBinaries() {
    std::string modulePath;
    std::unique_ptr<FileImage> fileImage;
    std::unique_ptr<ElfImage> elfImage;
    Reader reader(Base::_virtualAddressMap);
    typename SignatureDirectory::SignatureNameAndStatusConstIterator itEnd =
        Base::_signatureDirectory.EndSignatures();
    for (typename SignatureDirectory::SignatureNameAndStatusConstIterator it =
             Base::_signatureDirectory.BeginSignatures();
         it != itEnd; ++it) {
      typename SignatureDirectory::Status status = it->second.second;
      if (status != SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS &&
          status != SignatureDirectory::WRITABLE_MODULE_REFERENCE) {
        continue;
      }
      Offset signature = it->first;
      Offset relativeSignature;
      Offset rangeBase = 0;
      Offset rangeSize = 0;
      std::string newModulePath;
      if (!Base::_moduleDirectory.Find(signature, newModulePath, rangeBase,
                                       rangeSize, relativeSignature)) {
        continue;
      }
      if (newModulePath != modulePath) {
        modulePath = newModulePath;
        try {
          fileImage.reset(new FileImage(modulePath.c_str(), false));
          elfImage.reset(new ElfImage(*fileImage));
        } catch (...) {
          elfImage.reset(0);
          continue;
        }
      }
      if (elfImage == 0) {
        continue;
      }
      const VirtualAddressMap<Offset>& virtualAddressMap =
          elfImage->GetVirtualAddressMap();
      std::string typeinfoName;
      typeinfoName =
          GetUnmangledTypeinfoName(virtualAddressMap, relativeSignature);
      if (typeinfoName.empty()) {
        Offset typeinfoAddr = reader.ReadOffset(signature - sizeof(Offset), 0);
        if (typeinfoAddr == 0) {
          continue;
        }
        Offset mangledNameAddr =
            reader.ReadOffset(typeinfoAddr + sizeof(Offset), 0);
        if (mangledNameAddr == 0) {
          continue;
        }
        Offset relativeNameAddr;
        if (!Base::_moduleDirectory.Find(mangledNameAddr, newModulePath,
                                         rangeBase, rangeSize,
                                         relativeNameAddr)) {
          continue;
        }
        if (newModulePath == modulePath) {
          typeinfoName = CopyAndUnmangle(virtualAddressMap, relativeNameAddr);
        } else {
          try {
            std::unique_ptr<FileImage> fileImageForName(
                new FileImage(newModulePath.c_str(), false));
            std::unique_ptr<ElfImage> elfImageForName(
                new ElfImage(*fileImageForName));
            typeinfoName = CopyAndUnmangle(
                elfImageForName->GetVirtualAddressMap(), relativeNameAddr);
          } catch (...) {
          }
        }
      }
      if (!typeinfoName.empty()) {
        Base::_signatureDirectory.MapSignatureNameAndStatus(
            signature, typeinfoName,
            SignatureDirectory::VTABLE_WITH_NAME_FROM_BINARY);
      }
    }
  }

  void AddSignatureRequestsToSymReqs(std::ofstream& gdbScriptFile) {
    typename SignatureDirectory::SignatureNameAndStatusConstIterator itEnd =
        Base::_signatureDirectory.EndSignatures();
    for (typename SignatureDirectory::SignatureNameAndStatusConstIterator it =
             Base::_signatureDirectory.BeginSignatures();
         it != itEnd; ++it) {
      Offset signature = it->first;
      typename SignatureDirectory::Status status = it->second.second;
      if (status == SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS ||
          status == SignatureDirectory::WRITABLE_MODULE_REFERENCE) {
        gdbScriptFile << "printf \"SIGNATURE " << std::hex << signature
                      << "\\n\"" << '\n'
                      << "info symbol 0x" << signature << '\n';
      }
    }
  }

  void AddAnchorRequestsToSymReqs(std::ofstream& gdbScriptFile) {
    const Allocations::Graph<Offset>& graph = *(Base::_allocationGraph);
    const Allocations::Directory<Offset>& directory =
        Base::_allocationDirectory;
    typename Allocations::Directory<Offset>::AllocationIndex numAllocations =
        directory.NumAllocations();
    for (typename Allocations::Directory<Offset>::AllocationIndex i = 0;
         i < numAllocations; ++i) {
      const typename Allocations::Directory<Offset>::Allocation* allocation =
          directory.AllocationAt(i);
      if (!allocation->IsUsed() || !graph.IsStaticAnchorPoint(i)) {
        continue;
      }
      const std::vector<Offset>* anchors = graph.GetStaticAnchors(i);
      typename std::vector<Offset>::const_iterator itAnchorsEnd =
          anchors->end();
      for (typename std::vector<Offset>::const_iterator itAnchors =
               anchors->begin();
           itAnchors != itAnchorsEnd; ++itAnchors) {
        gdbScriptFile << "printf \"ANCHOR " << std::hex << *itAnchors << "\\n\""
                      << '\n'
                      << "info symbol 0x" << std::hex << *itAnchors << '\n';
      }
    }
  }
  void WriteSymreqsFileIfNeeded() {
    std::string symReqsPath(
        Base::_virtualAddressMap.GetFileImage().GetFileName());
    symReqsPath.append(".symreqs");
    std::ifstream symReqs;
    symReqs.open(symReqsPath.c_str());
    if (!symReqs.fail()) {
      return;
    }

    std::ofstream gdbScriptFile;
    gdbScriptFile.open(symReqsPath.c_str());
    if (gdbScriptFile.fail()) {
      std::cerr << "Unable to open " << symReqsPath << " for writing.\n";
      return;
    }

    std::string symDefsPath(
        Base::_virtualAddressMap.GetFileImage().GetFileName());
    symDefsPath.append(".symdefs");

    gdbScriptFile << "set logging file " << symDefsPath << '\n';
    gdbScriptFile << "set logging overwrite 1\n";
    gdbScriptFile << "set logging redirect 1\n";
    gdbScriptFile << "set logging on\n";
    gdbScriptFile << "set height 0\n";
    AddSignatureRequestsToSymReqs(gdbScriptFile);
    AddAnchorRequestsToSymReqs(gdbScriptFile);
    gdbScriptFile << "set logging off\n";
    gdbScriptFile << "set logging overwrite 0\n";
    gdbScriptFile << "set logging redirect 0\n";
    gdbScriptFile << "printf \"output written to " << symDefsPath << "\\n\""
                  << '\n';
    gdbScriptFile.close();
  }

  void FindStaticAnchorRanges() {
    for (const auto& range :
         Base::_virtualMemoryPartition.GetStaticAnchorCandidates()) {
      _staticAnchorLimits[range._base] = range._limit;
    }
  }
};
}  // namespace Linux
}  // namespace chap
