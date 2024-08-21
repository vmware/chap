// Copyright (c) 2017-2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include <map>
#include <regex>
#include "../Allocations/TaggerRunner.h"
#include "../CPlusPlus/Unmangler.h"
#include "../FileMappedRangeDirectory.h"
#include "../LibcMalloc/FinderGroup.h"
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"
#include "ELFModuleImageFactory.h"

namespace chap {
namespace Linux {
template <class ElfImage>
class ModuleFinder {
 public:
  typedef typename ElfImage::Offset Offset;
  ModuleFinder(VirtualMemoryPartition<Offset>& virtualMemoryPartition,
               FileMappedRangeDirectory<Offset>& fileMappedRangeDirectory,
               ModuleDirectory<Offset>& moduleDirectory)
      : _virtualMemoryPartition(virtualMemoryPartition),
        _virtualAddressMap(virtualMemoryPartition.GetAddressMap()),
        _fileMappedRangeDirectory(fileMappedRangeDirectory),
        _moduleDirectory(moduleDirectory),
        _linkMapChainHead(0),
        _baseInLinkMap(0),
        _limitInLinkMap(0),
        _executableLimitInLinkMap(0) {}

  void FindModules() {
    if (FindLinkMapChainByMappedFiles() ||
        FindLinkMapChainByShortUnclaimedWritableRanges()) {
      if (!DeriveLinkMapOffsets()) {
        std::cerr << "Warning: Failed to derive link map offsets.  Modules "
                     "will not be found.\n";
        return;
      }

      FindModulesByLinkMapChain();
    }
    if (_moduleDirectory.empty() && !TreatFirstMappedFileAsModule()) {
      std::cerr << "Warning: No modules were found.\n";
    }

    _moduleDirectory.Resolve();
  }

 private:
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  FileMappedRangeDirectory<Offset>& _fileMappedRangeDirectory;
  ModuleDirectory<Offset>& _moduleDirectory;
  Offset _linkMapChainHead;
  Offset _linkMapChainTail;
  static constexpr Offset ADJUST_IN_LINK_MAP = 0;
  static constexpr Offset NAME_IN_LINK_MAP = sizeof(Offset);
  static constexpr Offset DYNAMIC_IN_LINK_MAP = 2 * sizeof(Offset);
  static constexpr Offset NEXT_IN_LINK_MAP = 3 * sizeof(Offset);
  static constexpr Offset PREV_IN_LINK_MAP = 4 * sizeof(Offset);
  static constexpr Offset REAL_LINK_MAP_IN_LINK_MAP = 5 * sizeof(Offset);
  static constexpr Offset NAMESPACE_INDEX_IN_LINK_MAP = 6 * sizeof(Offset);
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef typename AddressMap::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename Allocations::SignatureDirectory<Offset> SignatureDirectory;
  typedef typename ElfImage::ElfHeader ElfHeader;
  typedef typename ElfImage::ProgramHeader ProgramHeader;
  typedef typename ElfImage::SectionHeader SectionHeader;
  typedef typename ElfImage::NoteHeader NoteHeader;
  typedef typename ElfImage::ElfDynamic ElfDynamic;

  Offset _baseInLinkMap;
  Offset _limitInLinkMap;
  Offset _executableLimitInLinkMap;

  bool FindLinkMapChainFromRange(Offset base, Offset limit) {
    typename VirtualAddressMap<Offset>::Reader reader(_virtualAddressMap);
    typename VirtualAddressMap<Offset>::Reader linkReader(_virtualAddressMap);
    for (typename VirtualAddressMap<Offset>::const_iterator it =
             _virtualAddressMap.find(base);
         it.Base() < limit; ++it) {
      if ((it.Flags() & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      Offset rangeLimit = it.Limit();
      Offset candidateLimit = rangeLimit - NAMESPACE_INDEX_IN_LINK_MAP;
      if (candidateLimit > rangeLimit) {
        continue;
      }
      for (Offset candidate = it.Base(); candidate < candidateLimit;
           candidate += sizeof(Offset)) {
        if ((reader.ReadOffset(candidate + REAL_LINK_MAP_IN_LINK_MAP, 0xbad) !=
             candidate) ||
            ((reader.ReadOffset(candidate, 0xbad) & 0xfff) != 0)) {
          continue;
        }
        size_t chainLength = 1;

        Offset link = reader.ReadOffset(candidate + PREV_IN_LINK_MAP, 0xbad);
        Offset prev = candidate;
        while (link != 0 && ++chainLength < 1000) {
          if ((linkReader.ReadOffset(link + REAL_LINK_MAP_IN_LINK_MAP, 0xbad) !=
               link) ||
              (linkReader.ReadOffset(link + NEXT_IN_LINK_MAP, 0xbad) != prev) ||
              ((linkReader.ReadOffset(link, 0xbad) & 0xfff) != 0)) {
            break;
          }
          prev = link;
          link = linkReader.ReadOffset(link + PREV_IN_LINK_MAP, 0xbad);
        }
        if (link != 0) {
          continue;
        }
        Offset chainHead = prev;

        link = reader.ReadOffset(candidate + NEXT_IN_LINK_MAP, 0xbad);
        prev = candidate;
        while (link != 0 && ++chainLength < 1000) {
          if ((linkReader.ReadOffset(link + REAL_LINK_MAP_IN_LINK_MAP, 0xbad) !=
               link) ||
              (linkReader.ReadOffset(link + PREV_IN_LINK_MAP, 0xbad) != prev) ||
              ((linkReader.ReadOffset(link, 0xbad) & 0xfff) != 0)) {
            break;
          }
          prev = link;
          link = linkReader.ReadOffset(link + NEXT_IN_LINK_MAP, 0xbad);
        }
        if (link != 0) {
          continue;
        }
        _linkMapChainHead = chainHead;
        _linkMapChainTail = prev;
        return true;
      }
    }
    return false;
  }

  bool FindLinkMapChainByMappedFiles() {
    for (const auto& range : _fileMappedRangeDirectory) {
      if (range._value._path.find("/ld") == std::string::npos) {
        continue;
      }
      if ((range._value._flags & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      if (FindLinkMapChainFromRange(range._base, range._limit)) {
        return true;
      }
    }
    return false;
  }

  bool TreatFirstMappedFileAsModule() {
    if (_fileMappedRangeDirectory.empty()) {
      return false;
    }
    auto it = _fileMappedRangeDirectory.begin();
    const std::string& path = it->_value._path;
    if (path.empty()) {
      return false;
    }

    // TODO: Fix this next statement so that it is possible to look at the
    // corresponding executable if it is present is consistent with what
    // is mapped in the core.
    _moduleDirectory.AddModule(path,
                               [](ModuleImage<Offset>&) { return false; });

    const char* image;
    Offset numImageBytes =
        _virtualAddressMap.FindMappedMemoryImage(it->_base, &image);
    if (FindRangesForModuleByMappedProgramHeaders(image, numImageBytes, path, 0,
                                                  it->_base, it->_limit)) {
      return true;
    }

    for (const auto& range : _fileMappedRangeDirectory) {
      if (range._value._path != path) {
        break;
      }
      // TODO: Try to extend the range if it is writable and the mapped
      // range from the file mapping doesn't reflect the full range (for
      // example, because some of the bss area is missing).
      _moduleDirectory.AddRange(range._base, range._limit - range._base, 0,
                                range._value._path, range._value._flags);
    }
    return true;
  }

  bool FindLinkMapChainByShortUnclaimedWritableRanges() {
    Reader reader(_virtualAddressMap);
    for (const auto& range :
         _virtualMemoryPartition.GetUnclaimedWritableRangesWithImages()) {
      Offset base = range._base;
      Offset limit = range._limit;
      if (limit - base >= 0x80000) {
        continue;
      }
      if (FindLinkMapChainFromRange(base, limit)) {
        return true;
      }
    }
    return false;
  }

  bool DeriveLinkMapOffsets() {
    size_t bestNumVotes = 0;
    size_t chainLength = 0;
    Offset bestCandidate = 0;
    typename VirtualAddressMap<Offset>::Reader reader(_virtualAddressMap);
    for (Offset linkMap = _linkMapChainHead; linkMap != 0;
         linkMap = reader.ReadOffset(linkMap + NEXT_IN_LINK_MAP)) {
      chainLength++;
    }
    for (Offset candidate = 0x40 * sizeof(Offset);
         candidate < 0x80 * sizeof(Offset); candidate += sizeof(Offset)) {
      size_t numVotes = 0;

      typename VirtualAddressMap<Offset>::Reader reader(_virtualAddressMap);
      for (Offset linkMap = _linkMapChainHead; linkMap != 0;
           linkMap = reader.ReadOffset(linkMap + NEXT_IN_LINK_MAP)) {
        Offset dynamic = reader.ReadOffset(linkMap + DYNAMIC_IN_LINK_MAP, 0);
        Offset base = reader.ReadOffset(linkMap + candidate, 1);
        if (base == 0 || base >= dynamic) {
          continue;
        }
        if ((base & (0xfff)) != 0) {
          continue;
        }
        Offset limit =
            reader.ReadOffset(linkMap + candidate + sizeof(Offset), 0);
        if (limit <= dynamic) {
          continue;
        }
        numVotes++;
      }
      if (numVotes > bestNumVotes) {
        bestNumVotes = numVotes;
        bestCandidate = candidate;
        if (bestNumVotes == chainLength) {
          break;
        }
      }
    }
    if (bestNumVotes + 1 < chainLength) {
      return false;
    }
    _baseInLinkMap = bestCandidate;
    _limitInLinkMap = bestCandidate + sizeof(Offset);
    _executableLimitInLinkMap = bestCandidate + 2 * sizeof(Offset);
    return true;
  }

  std::string FindModuleNameFromFileMappedRangeDirectory(Offset base,
                                                         Offset limit) {
    typename FileMappedRangeDirectory<Offset>::const_iterator it =
        _fileMappedRangeDirectory.upper_bound(base);

    if (it != _fileMappedRangeDirectory.end() && it->_base < limit) {
      // Note that VDSO is in memory but not actually
      // mapped to a file.
      return it->_value._path;
    }
    return "";
  }

  std::string FindModuleNameByMappedSONAME(const char* image,
                                           Offset numImageBytes, Offset dynamic,
                                           Offset adjust, Offset base,
                                           Offset limit) {
    std::string emptyName;
    if (numImageBytes < 0x1000) {
      return emptyName;
    }
    const ElfHeader* elfHeader = (const ElfHeader*)(image);
    if (elfHeader->e_type != ET_EXEC && elfHeader->e_type != ET_DYN) {
      return emptyName;
    }
    int entrySize = elfHeader->e_phentsize;
    Offset minimumExpectedRegionSize =
        elfHeader->e_phoff + (elfHeader->e_phnum * entrySize);
    const char* headerImage = image + elfHeader->e_phoff;
    const char* headerLimit = image + minimumExpectedRegionSize;
    if (numImageBytes < minimumExpectedRegionSize) {
      return emptyName;
    }

    Offset dynamicSize = 0;
    for (; headerImage < headerLimit; headerImage += entrySize) {
      ProgramHeader* programHeader = (ProgramHeader*)(headerImage);
      if (programHeader->p_type == PT_DYNAMIC) {
        if ((programHeader->p_vaddr + adjust) != dynamic) {
          continue;
        }
        dynamicSize = programHeader->p_memsz;
        break;
      }
    }
    if (dynamicSize == 0) {
      return emptyName;
    }

    Offset dynStrAddr = 0;
    Offset nameInDynStr = (Offset)(~0);

    int numDyn = dynamicSize / sizeof(ElfDynamic);
    const char* dynImage = 0;
    Offset numBytesFound =
        _virtualAddressMap.FindMappedMemoryImage(dynamic, &dynImage);
    if (numBytesFound < dynamicSize) {
      numDyn = numBytesFound / sizeof(ElfDynamic);
    }
    const ElfDynamic* dyn = (ElfDynamic*)(dynImage);
    const ElfDynamic* dynLimit = dyn + numDyn;
    for (; dyn < dynLimit; dyn++) {
      if (dyn->d_tag == DT_STRTAB) {
        dynStrAddr = (Offset)dyn->d_un.d_ptr;
      } else if (dyn->d_tag == DT_SONAME) {
        nameInDynStr = (Offset)dyn->d_un.d_ptr;
      }
    }

    if ((nameInDynStr == (Offset)(~0)) || (dynStrAddr == 0)) {
      return emptyName;
    }

    return ReadNameFromAddress(adjust + dynStrAddr + nameInDynStr);
  }

  /*
   * Record that a module range was present in the process, regardless of
   * whether it was actually present in the core.  If the range was not
   * present in the core, base the flags on the program header.
   */
  void AddRangeFromProgramHeader(Offset base, Offset size, Offset adjust,
                                 const std::string& path,
                                 int flagsFromProgramHeader) {
    int flagsIfUnmapped = RangeAttributes::HAS_KNOWN_PERMISSIONS;
    if ((flagsFromProgramHeader & PF_R) != 0) {
      flagsIfUnmapped |= RangeAttributes::IS_READABLE;
    }
    if ((flagsFromProgramHeader & PF_W) != 0) {
      flagsIfUnmapped |= RangeAttributes::IS_WRITABLE;
    }
    if ((flagsFromProgramHeader & PF_X) != 0) {
      flagsIfUnmapped |= RangeAttributes::IS_EXECUTABLE;
    }
    Offset limit = base + size;
    auto itEnd = _virtualAddressMap.end();
    Offset subrangeBase = base;
    for (auto it = _virtualAddressMap.upper_bound(base);
         it != itEnd && it.Base() < limit; ++it) {
      Offset mappedBase = it.Base();
      if (subrangeBase < mappedBase) {
        // The range to be registered was not included in the core.
        // However, we know it was actually present in the process and
        // know what the permissions were.
        _moduleDirectory.AddRange(subrangeBase, mappedBase - subrangeBase,
                                  adjust, path, flagsIfUnmapped);
        subrangeBase = mappedBase;
      }
      Offset subrangeLimit = it.Limit();
      if (subrangeLimit > limit) {
        subrangeLimit = limit;
      }
      _moduleDirectory.AddRange(subrangeBase, subrangeLimit - subrangeBase,
                                adjust, path, it.Flags());
      if (subrangeLimit == limit) {
        return;
      }
      subrangeBase = subrangeLimit;
    }
    _moduleDirectory.AddRange(subrangeBase, limit - subrangeBase, adjust, path,
                              flagsIfUnmapped);
  }

  void ClaimModuleAlignmentGapIfCompatible(Offset base, Offset size,
                                           const std::string& path) {
    Offset limit = base + size;
    typename VirtualAddressMap<Offset>::const_iterator itVirt =
        _virtualAddressMap.upper_bound(base);
    typename VirtualAddressMap<Offset>::const_iterator itVirtEnd =
        _virtualAddressMap.end();
    for (; itVirt != itVirtEnd; ++itVirt) {
      Offset rangeBase = itVirt.Base();
      if (rangeBase >= limit) {
        // We have passed the last range known to the VirtualAddressMap
        // that overlaps with the proposed gap.
        break;
      }
      if (rangeBase < base) {
        rangeBase = base;
      }
      Offset rangeLimit = itVirt.Limit();
      if (rangeLimit > limit) {
        rangeLimit = limit;
      }
      int flags = itVirt.Flags();
      if ((flags & (RangeAttributes::IS_WRITABLE |
                    RangeAttributes::IS_EXECUTABLE)) != 0) {
        /*
         * The proposed alignment gap overlaps a region that is clearly used
         * for something else.
         */
        return;
      }
      if ((flags & RangeAttributes::IS_READABLE) != 0) {
        /*
         * The proposed alignment gap overlaps a region that is marked as
         * readable.  If the entire gap is mapped and 0-filled, this may be
         * due to a bug in the creation of cores that can result in an
         * inaccessible region being marked as read-only.
         */
        if (rangeBase > base || rangeLimit < limit) {
          return;
        }
        if ((flags & RangeAttributes::IS_MAPPED) != 0) {
          /*
           * The region is clearly used for something else, because in the
           * case of the known bug in core creation, the inaccessible region
           * would be mapped.
           */
          return;
        }
        const char* image;
        Offset numBytesFound =
            _virtualAddressMap.FindMappedMemoryImage(base, &image);
        if (numBytesFound < size) {
          return;
        }
        for (const char* mustBe0 = image + size; --mustBe0 >= image;) {
          if (*mustBe0 != '\000') {
            return;
          }
        }
      }
    }
    if (!_virtualMemoryPartition.ClaimRange(
            base, size, _moduleDirectory.MODULE_ALIGNMENT_GAP, false)) {
      std::cerr << "Warning: unexpected overlap found for [0x" << std::hex
                << base << ", 0x" << size << ")\nalignment gap for module "
                << path << "\n";
    }
  }

  /*
   * Add ranges for the given module based on program headers for that module.
   */
  bool FindRangesForModuleByMappedProgramHeaders(const char* image,
                                                 Offset numImageBytes,
                                                 const std::string& path,
                                                 Offset adjust, Offset base,
                                                 Offset limit) {
    if (numImageBytes < sizeof(ElfHeader)) {
      return false;
    }
    const ElfHeader* elfHeader = (const ElfHeader*)(image);
    if (elfHeader->e_type != ET_EXEC && elfHeader->e_type != ET_DYN) {
      std::cerr
          << "The ELF type of module " << path
          << " does not appear to be for an executable or shared library.\n";
      return false;
    }
    int entrySize = elfHeader->e_phentsize;
    Offset minimumExpectedRegionSize =
        elfHeader->e_phoff + (elfHeader->e_phnum * entrySize);
    const char* headerImage = image + elfHeader->e_phoff;
    const char* headerLimit = image + minimumExpectedRegionSize;
    if (numImageBytes < minimumExpectedRegionSize) {
      std::cerr << "Contiguous image of module at 0x" << std::hex << base
                << " is only " << numImageBytes << " bytes.\n";
      return false;
    }

    Offset prevRangeLimit = 0;
    for (; headerImage < headerLimit; headerImage += entrySize) {
      ProgramHeader* programHeader = (ProgramHeader*)(headerImage);
      if (programHeader->p_type == PT_LOAD) {
        Offset vAddrFromPH = programHeader->p_vaddr;
        Offset rangeBase = (vAddrFromPH & ~0xfff) + adjust;
        Offset rangeLimit =
            ((vAddrFromPH + programHeader->p_memsz + 0xfff) & ~0xfff) + adjust;
        Offset align = programHeader->p_align;
        if ((prevRangeLimit > 0) && (align > 1)) {
          Offset gap = rangeBase - prevRangeLimit;
          if ((gap > 0) && ((gap == align) || (gap == (align - 0x1000)))) {
            ClaimModuleAlignmentGapIfCompatible(prevRangeLimit, gap, path);
          }
        }
        AddRangeFromProgramHeader(rangeBase, rangeLimit - rangeBase, adjust,
                                  path, programHeader->p_flags);
        prevRangeLimit = rangeLimit;
      }
    }
    return true;
  }

  bool FindRangesForModuleByModuleProgramHeaders(std::string& path,
                                                 Offset adjust,
                                                 Offset /* dynamic */,
                                                 Offset base, Offset limit) {
    const ModuleImage<Offset>* moduleImage =
        _moduleDirectory.GetModuleImage(path);
    if (moduleImage == nullptr) {
      return false;
    }
    const FileImage& fileImage = moduleImage->GetFileImage();
    const char* image = fileImage.GetImage();
    uint64_t fileSize = fileImage.GetFileSize();
    if (fileSize > ((Offset)(~0))) {
      abort();
    }

    return FindRangesForModuleByMappedProgramHeaders(image, (Offset)(fileSize),
                                                     path, adjust, base, limit);
  }

  bool FindRangesForModuleByLimitsFromLinkMap(std::string& path, Offset adjust,
                                              Offset dynamic, Offset base,
                                              Offset limit) {
    // TODO: possibly check contiguity from link_map (assumed at present).
    // TODO: if first range is not present, consider adding unmapped range
    //       of expected type.
    Offset prevRangeLimit = 0;
    typename VirtualAddressMap<Offset>::const_iterator itVirt =
        _virtualAddressMap.upper_bound(base);
    typename VirtualAddressMap<Offset>::const_iterator itVirtEnd =
        _virtualAddressMap.end();
    for (; itVirt != itVirtEnd; ++itVirt) {
      Offset rangeBase = itVirt.Base();
      if (rangeBase >= limit) {
        break;
      }
      if (rangeBase < base) {
        rangeBase = base;
      }
      Offset rangeLimit = itVirt.Limit();
      if (rangeLimit > limit) {
        rangeLimit = limit;
      }
      int flags = itVirt.Flags();
      if ((rangeLimit <= dynamic) &&
          ((flags & RangeAttributes::IS_WRITABLE) != 0)) {
        // This region does not belong to the module but was inserted into
        // an alignment gap.  Note that perhaps this could happen to a
        // non-writable region but the consequences of missing this are
        // much higher for a writable region.
        prevRangeLimit = limit;
        continue;
      }
      if (prevRangeLimit > 0) {
        Offset gap = rangeBase - prevRangeLimit;
        if ((gap == 0x200000) || (gap == 0x1ff000)) {
          ClaimModuleAlignmentGapIfCompatible(prevRangeLimit, gap, path);
        }
      }
      _moduleDirectory.AddRange(rangeBase, rangeLimit - rangeBase, adjust, path,
                                flags);
      prevRangeLimit = 0;
    }

    return true;
  }

  std::string ReadNameFromAddress(Offset nameAddress) {
    Reader reader(_virtualAddressMap);
    std::string path;
    char buffer[1000];

    size_t bytesRead = reader.ReadCString(nameAddress, buffer, sizeof(buffer));
    if ((bytesRead != 0) && (bytesRead != sizeof(buffer))) {
      path.assign(buffer, bytesRead);
    }
    return path;
  }

  void FindModuleByLinkMap(Offset linkMap, Offset adjust,
                           Offset nameAddressFromLinkMap, Offset dynamic,
                           Offset base, Offset limit) {
    if (base == 0 || base >= dynamic) {
      std::cerr << "Warning: base 0x" << std::hex << base
                << " is too low for linkmap at 0x" << std::hex << linkMap
                << "\n";
      return;
    }
    if ((base & (0xfff)) != 0) {
      std::cerr << "Warning: base 0x" << std::hex << base
                << " is not aligned for linkmap at 0x" << std::hex << linkMap
                << "\n";
      return;
    }
    if (limit <= dynamic) {
      std::cerr << "Warning: limit 0x" << std::hex << limit
                << " is not after dynamic area at 0x" << dynamic
                << " for linkmap at 0x" << std::hex << linkMap << "\n";
      return;
    }
    limit = (limit + 0xfff) & ~0xfff;

    const char* moduleHeaderImage = 0;
    Offset contiguousModuleHeaderImageBytes =
        _virtualAddressMap.FindMappedMemoryImage(base, &moduleHeaderImage);

    bool knownElfClassMismatch = false;
    if ((contiguousModuleHeaderImageBytes > EI_CLASS) &&
        (ElfImage::EXPECTED_ELF_CLASS !=
         ((const unsigned char*)moduleHeaderImage)[EI_CLASS])) {
      knownElfClassMismatch = true;
    }

    bool knownElfMagicMismatch = false;
    if ((contiguousModuleHeaderImageBytes > 0) &&
        (strncmp((const char*)moduleHeaderImage, ELFMAG, SELFMAG) != 0)) {
      knownElfMagicMismatch = true;
    }

    std::string path;
    bool rangeIsInFileMappedDirectory = false;
    bool fileMappedRangeDirectoryIsEmpty = _fileMappedRangeDirectory.empty();

    if (!fileMappedRangeDirectoryIsEmpty) {
      path = FindModuleNameFromFileMappedRangeDirectory(base, limit);
    }

    if (!path.empty()) {
      rangeIsInFileMappedDirectory = true;
    } else {
      path = ReadNameFromAddress(nameAddressFromLinkMap);
      if (path.empty()) {
        if (adjust == 0 || linkMap == _linkMapChainHead) {
          path = "main executable";
          // TODO: possibly get the main program name from the PT_NOTE section
        } else {
          path = FindModuleNameByMappedSONAME(moduleHeaderImage,
                                              contiguousModuleHeaderImageBytes,
                                              dynamic, adjust, base, limit);
          if (path.empty()) {
            std::cerr << "Warning: cannot figure out name for module with "
                         "link_map at 0x"
                      << std::hex << linkMap << ".\n";
            return;
          }
        }
      }
    }

    /*
     * We wait here to complain about an unexpected magic value or an
     * unexpected ELF class because we want to report the module path
     * at least partially.
     */

    if (knownElfMagicMismatch) {
      std::cerr << "The magic of module " << path
                << " is inconsistent with that of an ELF executable or "
                   "library.\nThis "
                   "module will be skipped.\n";
      return;
    }
    if (knownElfClassMismatch) {
      std::cerr << "The ELF class of module " << path
                << " is inconsistent with that of the process image.\nThis "
                   "module will be skipped.\n";
      return;
    }

    _moduleDirectory.AddModule(
        path, [adjust, dynamic, this](ModuleImage<Offset>& moduleImage) {
          const FileImage& fileImage = moduleImage.GetFileImage();
          const char* image = fileImage.GetImage();
          uint64_t fileSize = fileImage.GetFileSize();
          if (fileSize > ((Offset)(~0))) {
            return false;
          }
          Offset numImageBytes = fileSize;

          const ElfHeader* elfHeader = (const ElfHeader*)(image);
          int entrySize = elfHeader->e_phentsize;
          Offset minimumExpectedRegionSize =
              elfHeader->e_phoff + (elfHeader->e_phnum * entrySize);
          const char* headerImage = image + elfHeader->e_phoff;
          const char* headerLimit = image + minimumExpectedRegionSize;
          if (numImageBytes < minimumExpectedRegionSize) {
            return false;
          }

          Offset dynamicSize = 0;
          for (; headerImage < headerLimit; headerImage += entrySize) {
            ProgramHeader* programHeader = (ProgramHeader*)(headerImage);
            if (programHeader->p_type == PT_DYNAMIC) {
              if ((programHeader->p_vaddr + adjust) != dynamic) {
                return false;
              }
              dynamicSize = programHeader->p_memsz;
              break;
            }
          }
          if (dynamicSize == 0) {
            return false;
          }

          Offset numBytesToCompare = dynamicSize;
          const char* dynamicImageFromCore;
          Offset numBytesFound = _virtualAddressMap.FindMappedMemoryImage(
              dynamic, &dynamicImageFromCore);
          if (numBytesToCompare > numBytesFound) {
            numBytesToCompare = numBytesFound;
          }
          const VirtualAddressMap<Offset>& moduleVirtualAddressMap =
              moduleImage.GetVirtualAddressMap();
          const char* dynamicImageFromModule;
          numBytesFound = moduleVirtualAddressMap.FindMappedMemoryImage(
              dynamic - adjust, &dynamicImageFromModule);
          if (numBytesToCompare > numBytesFound) {
            numBytesToCompare = numBytesFound;
          }
          if (numBytesToCompare == 0) {
            return false;
          }
          return memcmp(dynamicImageFromModule, dynamicImageFromCore,
                        numBytesToCompare) != 0;
        });

    if ((moduleHeaderImage != nullptr) &&
        FindRangesForModuleByMappedProgramHeaders(
            moduleHeaderImage, contiguousModuleHeaderImageBytes, path, adjust,
            base, limit)) {
      return;
    }
    if ((fileMappedRangeDirectoryIsEmpty || rangeIsInFileMappedDirectory) &&
        FindRangesForModuleByModuleProgramHeaders(path, adjust, dynamic, base,
                                                  limit)) {
      return;
    }
    if (FindRangesForModuleByLimitsFromLinkMap(path, adjust, dynamic, base,
                                               limit)) {
      return;
    }
    std::cerr << "Warning: unable to find ranges for module " << path << "\n";
  }

  void FindModulesByLinkMapChain() {
    Reader reader(_virtualAddressMap);
    for (Offset linkMap = _linkMapChainHead; linkMap != 0;
         linkMap = reader.ReadOffset(linkMap + NEXT_IN_LINK_MAP)) {
      Offset adjust = reader.ReadOffset(linkMap + ADJUST_IN_LINK_MAP, 0);
      Offset nameFromLinkMap = reader.ReadOffset(linkMap + NAME_IN_LINK_MAP);
      Offset dynamic = reader.ReadOffset(linkMap + DYNAMIC_IN_LINK_MAP, 0);
      Offset base = reader.ReadOffset(linkMap + _baseInLinkMap, 1);
      Offset limit = reader.ReadOffset(linkMap + _limitInLinkMap, 0);
      FindModuleByLinkMap(linkMap, adjust, nameFromLinkMap, dynamic, base,
                          limit);
    }
  }
};
}  // namespace Linux
}  // namespace chap
