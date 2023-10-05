// Copyright (c) 2017-2019,2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <cstdlib>
#include <map>
#include "ModuleImageFactory.h"
#include "RangeMapper.h"
#include "VirtualMemoryPartition.h"

namespace chap {
template <typename Offset>
class ModuleDirectory {
  static constexpr Offset MODULE_OFFSET_UNKNOWN = ~0;

 public:
  struct ModuleInfo;

  struct RangeInfo {
    RangeInfo(struct ModuleInfo& moduleInfo,
              Offset adjustToModuleVirtualAddress, int flags,
              bool isInCoreVirtualAddressMap)
        : _moduleInfo(&moduleInfo),
          _adjustToModuleVirtualAddress(adjustToModuleVirtualAddress),
          _flags(flags),
          _isInCoreVirtualAddressMap(isInCoreVirtualAddressMap) {}
    RangeInfo()
        : _moduleInfo(nullptr),
          _adjustToModuleVirtualAddress(MODULE_OFFSET_UNKNOWN),
          _flags(0),
          _isInCoreVirtualAddressMap(false) {}
    RangeInfo(const RangeInfo& other)
        : _moduleInfo(other._moduleInfo),
          _adjustToModuleVirtualAddress(other._adjustToModuleVirtualAddress),
          _flags(other._flags),
          _isInCoreVirtualAddressMap(other._isInCoreVirtualAddressMap) {}
    RangeInfo& operator=(const RangeInfo& other) {
      if (this != &other) {
        _moduleInfo = other._moduleInfo;
        _adjustToModuleVirtualAddress = other._adjustToModuleVirtualAddress;
        _flags = other._flags;
        _isInCoreVirtualAddressMap = other._isInCoreVirtualAddressMap;
      }
      return *this;
    }
    bool operator==(const RangeInfo& other) {
      return _moduleInfo == other._moduleInfo &&
             _adjustToModuleVirtualAddress ==
                 other._adjustToModuleVirtualAddress &&
             _flags == other._flags &&
             _isInCoreVirtualAddressMap == other._isInCoreVirtualAddressMap;
    }
    struct ModuleInfo* _moduleInfo;
    /*
     * This value is subtracted from a program virtual address to yield a
     * virtual address in the module for the given range.
     */
    Offset _adjustToModuleVirtualAddress;
    int _flags;  // The meanings of the bits match those in VirtualAddressMap.
    bool _isInCoreVirtualAddressMap;
  };
  typedef RangeMapper<Offset, RangeInfo> RangeToInfo;
  typedef RangeMapper<Offset, const ModuleInfo*> RangeToModuleInfoPointer;
  struct ModuleInfo {
    ModuleInfo(std::string modulePath) : _originalPath(modulePath) {}
    /*
     * This is where the module was found at the time the process was
     * running.
     */
    std::string _originalPath;
    /*
     * This is where the chap found a copy of the module, or is empty
     * if the module was not found.  In the most common case, where
     * the environment variable CHAP_MODULE_ROOTS is not set, the
     * _relocatedPath will match the _originalPath, unless
     * there is no file at that location.  If CHAP_MODULE_ROOTS is set,
     * chap will try to append the _original path to each of the entries
     * in CHAP_MODULE_ROOTS until it finds a file that exists with that
     * constructed path name.
     */
    std::string _relocatedPath;
    RangeToInfo _ranges;
    std::unique_ptr<ModuleImage<Offset> > _moduleImage;
  };
  typedef std::map<std::string, ModuleInfo> NameToModuleInfo;

  typedef typename NameToModuleInfo::iterator iterator;
  typedef typename NameToModuleInfo::const_iterator const_iterator;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  ModuleDirectory(VirtualMemoryPartition<Offset>& partition,
                  ModuleImageFactory<Offset>* moduleImageFactory)
      : MODULE_ALIGNMENT_GAP("module alignment gap"),
        USED_BY_MODULE("used by module"),
        _isResolved(false),
        _virtualMemoryPartition(partition),
        _virtualAddressMap(partition.GetAddressMap()),
        _moduleImageFactory(moduleImageFactory) {
    const char* chapModuleRoots = std::getenv("CHAP_MODULE_ROOTS");
    if (chapModuleRoots != nullptr) {
      std::string roots(chapModuleRoots);
      std::string::size_type pos = 0;
      while (true) {
        std::string::size_type colonPos = roots.find(pos, ':');
        bool lastRoot = (colonPos == std::string::npos);
        std::string root(lastRoot ? roots.substr(pos)
                                  : roots.substr(pos, colonPos - pos));
        _chapModuleRoots.emplace_back(root);
        if (lastRoot) {
          break;
        }
        pos = colonPos + 1;
      }
    } else {
      _chapModuleRoots.emplace_back("");
    }
  }
  void AddRange(Offset base, Offset size, Offset adjustToModuleVirtualAddress,
                const std::string& name, Offset flags) {
    if (_isResolved) {
      // The module directory cannot be changed after it has been resolved.
      abort();
    }
    auto emplaceResult = _nameToModuleInfo.emplace(name, name);
    ModuleInfo& moduleInfo = emplaceResult.first->second;
    if (emplaceResult.second) {
      for (const auto chapModuleRoot : _chapModuleRoots) {
        std::string relocatedPath = chapModuleRoot + name;
        moduleInfo._moduleImage.reset(
            _moduleImageFactory->MakeModuleImage(relocatedPath));
        if (moduleInfo._moduleImage.get() != nullptr) {
          moduleInfo._relocatedPath = relocatedPath;
          break;
        }
      }
    }
    Offset limit = base + size;
    auto itEnd = _virtualAddressMap.end();
    Offset subrangeBase = base;
    for (auto it = _virtualAddressMap.upper_bound(base);
         it != itEnd && it.Base() < limit; ++it) {
      if (subrangeBase < it.Base()) {
        // The range to be registered was not included in the core.
        if (flags != 0) {
          // However, we know it was actually present in the process and
          // know what the permissions were.
          int flagsForSubrange = (flags & ~RangeAttributes::IS_MAPPED) |
                                 RangeAttributes::HAS_KNOWN_PERMISSIONS;
          Offset subrangeLimit = it.Base();
          if (subrangeLimit > limit) {
            subrangeLimit = limit;
          }
          Offset subrangeSize = subrangeLimit - subrangeBase;
          auto mapRangeResult = _rangeToModuleInfoPointer.MapRange(
              subrangeBase, subrangeSize, &moduleInfo);
          if (mapRangeResult.second) {
            if (!moduleInfo._ranges
                     .MapRange(
                         subrangeBase, subrangeSize,
                         RangeInfo(moduleInfo, adjustToModuleVirtualAddress,
                                   flagsForSubrange, true))
                     .second) {
              /*
               * This should never happen because we have already checked that
               * the range doesn't overlap any range for any module.
               */
              abort();
            }
          } else {
            std::cerr << "Warning, range [0x" << std::hex << base << ", 0x"
                      << (base + size) << ")\nfor module " << name
                      << "\noverlaps some other mapped range in "
                         "_rangeToModuleInfoPointer.\n";
          }
        }
        subrangeBase = it.Base();
      }
      // [subrangeBase, it.Limit()) is known to the process image.
      Offset subrangeLimit = it.Limit();
      if (subrangeLimit > limit) {
        subrangeLimit = limit;
      }
      int flags = it.Flags();
      if ((flags &
           (RangeAttributes::IS_EXECUTABLE | RangeAttributes::IS_WRITABLE |
            RangeAttributes::IS_READABLE)) != 0) {
        // Count the range for now.  Actually registering the range in the
        // partition must be deferred at present both because there is
        // environment specific knowledge to calculate alignment gaps and
        // because such logic is best done after we know all the ranges
        // for the module.
        Offset subrangeSize = subrangeLimit - subrangeBase;
        auto mapRangeResult = _rangeToModuleInfoPointer.MapRange(
            subrangeBase, subrangeSize, &moduleInfo);
        if (mapRangeResult.second) {
          if (!moduleInfo._ranges
                   .MapRange(subrangeBase, subrangeSize,
                             RangeInfo(moduleInfo, adjustToModuleVirtualAddress,
                                       flags, true))
                   .second) {
            /*
             * This should never happen because we have already checked that
             * the range doesn't overlap any range for any module.
             */
            abort();
          }
        } else {
          std::cerr << "Warning, range [0x" << std::hex << base << ", 0x"
                    << (base + size) << ")\nfor module " << name
                    << "\noverlaps some other mapped range in "
                       "_rangeToModuleInfoPointer.\n";
        }
      }
      subrangeBase = subrangeLimit;
    }
  }

  const ModuleInfo* Find(const std::string& name) const {
    const_iterator it = _nameToModuleInfo.find(name);
    if (it != _nameToModuleInfo.end()) {
      return &(it->second);
    } else {
      return nullptr;
    }
  }

  bool Find(Offset addr, std::string& name, Offset& base, Offset& size,
            Offset& relativeVirtualAddress) const {
    const ModuleInfo* moduleInfo;
    Offset coallescedBase;  // Coallesced ignoring RangeInfo values
    Offset coallescedSize;
    if (!_rangeToModuleInfoPointer.FindRange(addr, coallescedBase,
                                             coallescedSize, moduleInfo)) {
      return false;
    }
    const auto& ranges = moduleInfo->_ranges;
    RangeInfo rangeInfo;
    if (!ranges.FindRange(addr, base, size, rangeInfo)) {
      return false;
    }
    name = moduleInfo->_originalPath;
    relativeVirtualAddress = addr - rangeInfo._adjustToModuleVirtualAddress;
    return true;
  }

  void Resolve() { _isResolved = true; }
  bool IsResolved() const { return _isResolved; }
  const_iterator begin() const { return _nameToModuleInfo.begin(); }
  const_iterator end() const { return _nameToModuleInfo.end(); }

  size_t NumModules() const { return _nameToModuleInfo.size(); }
  const char* MODULE_ALIGNMENT_GAP;
  const char* USED_BY_MODULE;
  const ModuleImage<Offset>* GetModuleImage(
      const std::string& modulePath) const {
    const auto& it = _nameToModuleInfo.find(modulePath);
    return (it == _nameToModuleInfo.end()) ? nullptr
                                           : it->second._moduleImage.get();
  }

 private:
  bool _isResolved;
  NameToModuleInfo _nameToModuleInfo;
  RangeToModuleInfoPointer _rangeToModuleInfoPointer;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  std::unique_ptr<ModuleImageFactory<Offset> > _moduleImageFactory;
  std::vector<std::string> _chapModuleRoots;
};
}  // namespace chap
