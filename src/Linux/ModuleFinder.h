// Copyright (c) 2017-2023 VMware, Inc. All Rights Reserved.
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

    for (typename ModuleDirectory<Offset>::const_iterator it =
             _moduleDirectory.begin();
         it != _moduleDirectory.end(); ++it) {
      // TODO: possibly this should be done in the individual range finding
      //       methods
      ClaimRangesForModule(it);
    }
    // TODO: possibly this should do less
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
      const std::string& path = range._value._path;
      if (path.find("/ld") == std::string::npos) {
        continue;
      }
      Offset base = range._base;
      Offset limit = range._limit;
      if ((range._value._flags & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      if (FindLinkMapChainFromRange(base, limit)) {
        return true;
      }
    }
    return false;
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

  // The image is of the start of a mapped image of the given module.
  // The module has already been checked to be of the expected
  // ELF class and the image has the correct ELF magic.
  bool FindRangesForModuleByMappedProgramHeaders(const char* image,
                                                 Offset numImageBytes,
                                                 std::string& path,
                                                 Offset adjust, Offset base,
                                                 Offset limit) {
    if (numImageBytes < 0x1000) {
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

    for (; headerImage < headerLimit; headerImage += entrySize) {
      ProgramHeader* programHeader = (ProgramHeader*)(headerImage);
      if (programHeader->p_type == PT_LOAD) {
        Offset vAddrFromPH = programHeader->p_vaddr;
        Offset rangeBase = (vAddrFromPH & ~0xfff) + adjust;
        Offset rangeLimit =
            ((vAddrFromPH + programHeader->p_memsz + 0xfff) & ~0xfff) + adjust;
        _moduleDirectory.AddRange(rangeBase, rangeLimit - rangeBase, adjust,
                                  path, programHeader->p_flags);
      }
    }
    return true;
  }

  bool FindRangesForModuleByModuleProgramHeaders(std::string& /* path */,
                                                 Offset /* adjust */,
                                                 Offset /* dynamic */,
                                                 Offset /* base */,
                                                 Offset /* limit */) {
    return false;
  }
  bool FindRangesForModuleByLimitsFromLinkMap(std::string& path, Offset adjust,
                                              Offset dynamic, Offset base,
                                              Offset limit) {
    // TODO: possibly check contiguity from link_map (assumed at present).
    // TODO: if first range is not present, consider adding unmapped range
    //       of expected type.
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
        continue;
      }
      _moduleDirectory.AddRange(rangeBase, rangeLimit - rangeBase, adjust, path,
                                flags);
    }

    return true;
  }

  std::string ReadNameFromAddress(Offset nameAddressFromLinkMap) {
    Reader reader(_virtualAddressMap);
    std::string path;
    char buffer[1000];

    size_t bytesRead =
        reader.ReadCString(nameAddressFromLinkMap, buffer, sizeof(buffer));
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
          // TODO: possibly get the path of the shared library using DT_SONAME
          // and DT_STRTAB, as seen in some older code in this file.
          std::cerr << "Warning: cannot figure out name for module with "
                       "link_map at 0x"
                    << std::hex << linkMap << ".\n";
          return;
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

    if ((moduleHeaderImage != nullptr) &&
        FindRangesForModuleByMappedProgramHeaders(
            moduleHeaderImage, contiguousModuleHeaderImageBytes, path, adjust,
            base, limit)) {
      return;
    }
    if ((fileMappedRangeDirectoryIsEmpty || rangeIsInFileMappedDirectory) &&
        FindRangesForModuleByModuleProgramHeaders(path, adjust, dynamic, base,
                                                  limit)) {
      // TODO: write the function
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
                           std::vector<ModuleRange>& unmappedRanges) {
    const auto& ranges = it->second._ranges;
    auto itRange = ranges.begin();
    const auto& itRangeEnd = ranges.end();
    Offset base = itRange->_base;
    Offset rangeLimit = itRange->_limit;
    typename VirtualAddressMap<Offset>::const_iterator itVirt =
        _virtualAddressMap.find(base);
    typename VirtualAddressMap<Offset>::const_iterator itVirtEnd =
        _virtualAddressMap.end();
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
      unmappedRanges.emplace_back(base, limit - base, permissionsBits, image);
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
        if (!_virtualMemoryPartition.ClaimRange(range._base, range._size,
                                                _moduleDirectory.USED_BY_MODULE,
                                                true)) {
          std::cerr << "Warning: unexpected overlap found for [0x" << std::hex
                    << range._base << ", 0x" << (range._base + range._size)
                    << ")\nused by module " << it->first << "\n";
        }

      } else if (range._permissions == (RangeAttributes::HAS_KNOWN_PERMISSIONS |
                                        RangeAttributes::IS_READABLE |
                                        RangeAttributes::IS_EXECUTABLE)) {
        if (!_virtualMemoryPartition.ClaimRange(range._base, range._size,
                                                _moduleDirectory.USED_BY_MODULE,
                                                true)) {
          std::cerr << "Warning: unexpected overlap found for [0x" << std::hex
                    << range._base << ", 0x" << (range._base + range._size)
                    << ")\nused by module " << it->first << "\n";
        }
      } else if (range._permissions == RangeAttributes::HAS_KNOWN_PERMISSIONS) {
        gapFound = true;
        if (!_virtualMemoryPartition.ClaimRange(
                range._base, range._size, _moduleDirectory.MODULE_ALIGNMENT_GAP,
                false)) {
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
            if (!_virtualMemoryPartition.ClaimRange(
                    range._base, range._size,
                    _moduleDirectory.MODULE_ALIGNMENT_GAP, false)) {
              std::cerr << "Warning: unexpected overlap found for [0x"
                        << std::hex << range._base << ", 0x"
                        << (range._base + range._size)
                        << ")\nalignment gap for module " << it->first << "\n";
            }
            gapFound = true;
            continue;
          }
        }
        if (!_virtualMemoryPartition.ClaimRange(range._base, range._size,
                                                _moduleDirectory.USED_BY_MODULE,
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
           * it is legitimately a gap, by making sure that there are no ranges
           * known to that core that lie in the given range.
           */
          gapFound = true;

          typename VirtualAddressMap<Offset>::const_iterator itVirt =
              _virtualAddressMap.upper_bound(gapBase);
          typename VirtualAddressMap<Offset>::const_iterator itVirtEnd =
              _virtualAddressMap.end();
          for (; itVirt != itVirtEnd && itVirt.Base() < gapLimit; ++itVirt) {
            if ((itVirt.Flags() & RangeAttributes::PERMISSIONS_MASK) !=
                RangeAttributes::HAS_KNOWN_PERMISSIONS) {
              gapFound = false;
              break;
            }
          }
          if (gapFound) {
            if (!_virtualMemoryPartition.ClaimRange(
                    gapBase, gapSize, _moduleDirectory.MODULE_ALIGNMENT_GAP,
                    false)) {
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
};
}  // namespace Linux
}  // namespace chap
