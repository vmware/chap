// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include <map>
#include "../Allocations/SignatureDirectory.h"
#include "../ProcessImage.h"
#include "../Unmangler.h"
#include "ELFImage.h"
#include "LibcMallocAllocationFinder.h"

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
       * Make the allocation finder eagerly unless chap is being used only
       * to determine whether the core is truncated, in which case there
       * won't be any need to find the allocations.
       */

      Base::_allocationFinder =
          new LibcMallocAllocationFinder<Offset>(Base::_virtualMemoryPartition);

      /*
       * Find any modules after checking for the libc malloc allocation
       * finder because the libc malloc allocation finder does not depend
       * on knowing whether or where any modules are present, and finding
       * libc allocations first considerably shortens the time to find the
       * modules because it allows skipping heaps and main arena allocation
       * runs.
       */
      FindModules();

      /*
       * This has to be done after the allocations are found because the
       * the current algorithm for static anchor ranges is to
       * assume that all imaged writeable memory that is not otherwise claimed
       * (for example by stack or memory allocators) is OK for anchors.
       * At some point, we should change the algorithm to use the module
       * ranges, but at present the module ranges are not found sufficiently
       * consistently to allow that.
       */
      FindStaticAnchorRanges();

      Base::_allocationGraph = new Allocations::Graph<Offset>(
          *Base::_allocationFinder, Base::_threadMap, _staticAnchorLimits,
          (Allocations::ExternalAnchorPointChecker<Offset>*)(0));

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
    }
  }

  template <typename T>
  struct CompareByAddressField {
    bool operator()(const T& left, const T& right) {
      return left._address < right._address;
    }
  };

  const std::map<Offset, Offset>& GetStaticAnchorLimits() const {
    return _staticAnchorLimits;
  }

 protected:
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
      const char* name = (char*)(0);
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
        Offset numBytesFound =
            Base::_virtualAddressMap.FindMappedMemoryImage(nameStart, &name);
        if (numBytesFound < 2 || name[0] == 0) {
          std::cerr << "Module chain entry at 0x" << std::hex << link
                    << " has an empty or invalid name.\n";
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
        Offset size = entry[1] - base;
        Base::_moduleDirectory.AddRange(base, size, std::string(stringStart));
        stringStart += strlen(stringStart) + 1;
      }
      return true;
    }
    return false;
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
    return _elfImage.VisitNotes(std::bind(
        &LinuxProcessImage<ElfImage>::FindModulesFromELIFNote, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  }

  /*
  * Try to find a loader chain with the guess that at least one member has
  * the expected alignment, which is assumed but not checked to be a power
  * of 2.
  */
  bool FindModulesByAlignedLink(Offset expectedAlignment) {
    typename VirtualAddressMap<Offset>::Reader reader(Base::_virtualAddressMap);
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator
        itEnd = Base::_virtualMemoryPartition.EndUnclaimedImages();
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator it =
        Base::_virtualMemoryPartition.BeginUnclaimedImages();
    for (; it != itEnd; ++it) {
      if ((it->_value &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) !=
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
        continue;
      }
      Offset base = it->_base;
      Offset align = base & (expectedAlignment - 1);
      if ((align) != 0) {
        base += (expectedAlignment - align);
      }
      Offset limit = it->_limit - 0x2f;
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

      const char* name = (char*)(0);
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
      if (name == (char*)(0)) {
        if (dynStrAddr != 0 && nameInDynStr != 0) {
          Offset numBytesFound = Base::_virtualAddressMap.FindMappedMemoryImage(
              dynStrAddr + nameInDynStr, &name);
          if (numBytesFound < 2) {
            name = (char*)(0);
          }
        }
      }
      if (name == (char*)(0)) {
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
  void FindModules() {
    if (!FindModulesByPTNote() && !FindModulesByAlignedLink(0x1000) &&
        !FindModulesByAlignedLink(sizeof(Offset))) {
      FindModulesByMappedImages();
    }
  }

  void RefreshSignatureDirectory() const {
    if (Base::_allocationFinder == 0) {
      return;
    }
    if (!_symdefsRead) {
      ReadSymdefsFile();
    }
  }

 private:
  /*
   * _symdefsRead is mutable because the symdefs file is read lazily the
   * first time it is present and needed.
   */

  ElfImage& _elfImage;
  mutable bool _symdefsRead;
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
      if (image != (const char*)(0)) {
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

    typename VirtualAddressMap<Offset>::Reader reader(virtualAddressMap);
    Offset typeInfoAddress = reader.ReadOffset(typeInfoPointerAddress, 0);
    if (typeInfoAddress == 0) {
      return emptySignatureName;
    }
    Offset typeInfoNameAddress =
        reader.ReadOffset(typeInfoAddress + sizeof(Offset), 0);
    if (typeInfoNameAddress != 0) {
      return CopyAndUnmangle(virtualAddressMap, typeInfoNameAddress);
    }
    return emptySignatureName;
  }

  bool ReadSymdefsFile() const {
    // TODO: This implies just one image per file.
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
    const Allocations::Finder<Offset>& finder = *(Base::_allocationFinder);
    typename Allocations::Finder<Offset>::AllocationIndex numAllocations =
        finder.NumAllocations();
    typename VirtualAddressMap<Offset>::Reader reader(Base::_virtualAddressMap);
    typename VirtualAddressMap<Offset>::const_iterator itEnd =
        Base::_virtualAddressMap.end();
    for (typename Allocations::Finder<Offset>::AllocationIndex i = 0;
         i < numAllocations; ++i) {
      const typename Allocations::Finder<Offset>::Allocation* allocation =
          finder.AllocationAt(i);
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

      if ((it.Flags() & RangeAttributes::IS_WRITABLE) != 0) {
        continue;
      }

      std::string typeinfoName =
          GetUnmangledTypeinfoName(Base::_virtualAddressMap, signature);

      Base::_signatureDirectory.MapSignatureNameAndStatus(
          signature, typeinfoName,
          typeinfoName.empty()
              ? SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS
              : SignatureDirectory::VTABLE_WITH_NAME_FROM_PROCESS_IMAGE);
    }
  }

  void FindSignatureNamesFromBinaries() {
    std::string modulePath;
    std::unique_ptr<FileImage> fileImage;
    std::unique_ptr<ElfImage> elfImage;
    typename VirtualAddressMap<Offset>::Reader reader(Base::_virtualAddressMap);
    typename SignatureDirectory::SignatureNameAndStatusConstIterator itEnd =
        Base::_signatureDirectory.EndSignatures();
    for (typename SignatureDirectory::SignatureNameAndStatusConstIterator it =
             Base::_signatureDirectory.BeginSignatures();
         it != itEnd; ++it) {
      typename SignatureDirectory::Status status = it->second.second;
      if (status != SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS) {
        continue;
      }
      Offset signature = it->first;
      Offset fileOffset;
      Offset relativeSignature;
      Offset rangeBase = 0;
      Offset rangeSize = 0;
      std::string newModulePath;
      if (!Base::_moduleDirectory.Find(signature, newModulePath, rangeBase,
                                       rangeSize, fileOffset,
                                       relativeSignature)) {
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
                                         rangeBase, rangeSize, fileOffset,
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
      if (status == SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS) {
        gdbScriptFile << "printf \"SIGNATURE " << std::hex << signature
                      << "\\n\"" << '\n'
                      << "info symbol 0x" << signature << '\n';
      }
    }
  }

  void AddAnchorRequestsToSymReqs(std::ofstream& gdbScriptFile) {
    const Allocations::Graph<Offset>& graph = *(Base::_allocationGraph);
    const Allocations::Finder<Offset>& finder = *(Base::_allocationFinder);
    typename Allocations::Finder<Offset>::AllocationIndex numAllocations =
        finder.NumAllocations();
    for (typename Allocations::Finder<Offset>::AllocationIndex i = 0;
         i < numAllocations; ++i) {
      const typename Allocations::Finder<Offset>::Allocation* allocation =
          finder.AllocationAt(i);
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
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator
        itEnd = Base::_virtualMemoryPartition.EndUnclaimedImages();
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator it =
        Base::_virtualMemoryPartition.BeginUnclaimedImages();
    for (; it != itEnd; ++it) {
      if ((it->_value &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) ==
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
        _staticAnchorLimits[it->_base] = it->_limit;
      }
    }
  }
};
}  // namespace Linux
}  // namespace chap
