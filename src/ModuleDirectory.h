// Copyright (c) 2017-2019,2023,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
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
          _flags(flags) {}
    RangeInfo()
        : _moduleInfo(nullptr),
          _adjustToModuleVirtualAddress(MODULE_OFFSET_UNKNOWN),
          _flags(0) {}
    RangeInfo(const RangeInfo& other)
        : _moduleInfo(other._moduleInfo),
          _adjustToModuleVirtualAddress(other._adjustToModuleVirtualAddress),
          _flags(other._flags) {}
    RangeInfo& operator=(const RangeInfo& other) {
      if (this != &other) {
        _moduleInfo = other._moduleInfo;
        _adjustToModuleVirtualAddress = other._adjustToModuleVirtualAddress;
        _flags = other._flags;
      }
      return *this;
    }
    bool operator==(const RangeInfo& other) {
      return _moduleInfo == other._moduleInfo &&
             _adjustToModuleVirtualAddress ==
                 other._adjustToModuleVirtualAddress &&
             _flags == other._flags;
    }
    struct ModuleInfo* _moduleInfo;
    /*
     * This value is subtracted from a program virtual address to yield a
     * virtual address in the module for the given range.
     */
    Offset _adjustToModuleVirtualAddress;
    int _flags;  // The meanings of the bits match those in VirtualAddressMap.
  };
  typedef RangeMapper<Offset, RangeInfo> RangeToInfo;
  typedef RangeMapper<Offset, const ModuleInfo*> RangeToModuleInfoPointer;
  struct ModuleInfo {
    ModuleInfo(std::string modulePath) : _runtimePath(modulePath) {}
    /*
     * This is where the module was located at the time the process was
     * running.
     */
    std::string _runtimePath;
    RangeToInfo _ranges;
    /*
     * This allows one to look at the module itself, to fill in for items
     * that are missing in the core but that can be obtained from the
     * module itself.  One can obtain the path used by chap, which may
     * differ from the path used by the process at runtime if
     * CHAP_MODULE_ROOTS is set, by calling GetPath() on the _moduleImage.
     */
    std::unique_ptr<ModuleImage<Offset> > _moduleImage;
    /*
     * These are paths that were checked but rejected because they appear
     * to be from a different version of the given module.
     */
    std::vector<std::string> _incompatiblePaths;
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

  void AddModule(const std::string& runtimePath,
                 std::function<bool(ModuleImage<Offset>&)> checkImage) {
    if (_isResolved) {
      // The module directory cannot be changed after it has been resolved.
      abort();
    }
    auto emplaceResult = _nameToModuleInfo.emplace(runtimePath, runtimePath);
    if (runtimePath[0] != '/') {
      return;
    }
    ModuleInfo& moduleInfo = emplaceResult.first->second;
    if (emplaceResult.second) {
      for (const auto chapModuleRoot : _chapModuleRoots) {
        std::string relocatedPath = chapModuleRoot + runtimePath;
        ModuleImage<Offset>* moduleImage =
            _moduleImageFactory->MakeModuleImage(relocatedPath);
        if (moduleImage == nullptr) {
          continue;
        }
        if (checkImage(*moduleImage)) {
          moduleInfo._moduleImage.reset(moduleImage);
          break;
        }
        delete moduleImage;
        moduleInfo._incompatiblePaths.push_back(relocatedPath);
      }
    }
  }

  void AddRange(Offset base, Offset size, Offset adjustToModuleVirtualAddress,
                const std::string& name, Offset flags) {
    if (_isResolved) {
      // The module directory cannot be changed after it has been resolved.
      std::cerr << "Fatal Error: Added a module range for module "
                << "\"" << name
                << "\" after the module directory was resolved.\n";
      abort();
    }
    iterator it = _nameToModuleInfo.find(name);
    if (it == _nameToModuleInfo.end()) {
      std::cerr << "Fatal Error: Added a module range before adding module "
                << "\"" << name << "\".\n";
      abort();
    }
    ModuleInfo& moduleInfo = it->second;
    auto mapRangeResult =
        _rangeToModuleInfoPointer.MapRange(base, size, &moduleInfo);
    if (mapRangeResult.second) {
      if (!moduleInfo._ranges
               .MapRange(base, size,
                         RangeInfo(moduleInfo, adjustToModuleVirtualAddress,
                                   flags, true))
               .second) {
        /*
         * This should never happen because we have already checked that
         * the range doesn't overlap any range for any module.
         */
        std::cerr << "Fatal Error: Corruption found in range info for module "
                  << "\"" << name << "\".\n";
        abort();
      }
      if (!_virtualMemoryPartition.ClaimRange(base, size, USED_BY_MODULE, flags,
                                              true)) {
        std::cerr << "Warning: unexpected overlap found for [0x" << std::hex
                  << base << ", 0x" << (base + size) << ")\nused by module "
                  << it->first << "\n";
      }
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
    name = moduleInfo->_runtimePath;
    relativeVirtualAddress = addr - rangeInfo._adjustToModuleVirtualAddress;
    return true;
  }

  void Resolve() { _isResolved = true; }
  bool IsResolved() const { return _isResolved; }
  const_iterator begin() const { return _nameToModuleInfo.begin(); }
  const_iterator end() const { return _nameToModuleInfo.end(); }
  bool empty() const { return _nameToModuleInfo.empty(); }

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
