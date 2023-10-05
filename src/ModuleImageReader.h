// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "ModuleDirectory.h"
#include "VirtualAddressMap.h"

namespace chap {
template <typename Offset>
class ModuleImageReader {
 public:
  ModuleImageReader(const ModuleDirectory<Offset>& moduleDirectory)
      : _moduleDirectory(moduleDirectory),
        _currentModuleImage(nullptr),
        _rangeStart(0),
        _rangeLimit(0) {}

  size_t ReadCString(Offset address, char* buffer, size_t bufferSize) {
    if (!SetRelativeVirtualAddressAndAddressMap(address)) {
      return (size_t)(0);
    }
    return _currentReader->ReadCString(_currentRelativeVirtualAddress, buffer,
                                       bufferSize);
  }

 private:
  const ModuleDirectory<Offset>& _moduleDirectory;
  std::string _currentModulePath;
  const ModuleImage<Offset>* _currentModuleImage;
  const VirtualAddressMap<Offset>* _currentVirtualAddressMap;
  Offset _rangeStart;
  Offset _rangeLimit;
  Offset _currentRelativeVirtualAddress;
  Offset _addressAdjustment;
  std::unique_ptr<typename VirtualAddressMap<Offset>::Reader> _currentReader;

  bool SetRelativeVirtualAddressAndAddressMap(Offset address) {
    if (address < _rangeStart || address >= _rangeLimit) {
      Offset rangeBase = 0;
      Offset rangeSize = 0;
      std::string newModulePath;
      if (!_moduleDirectory.Find(address, newModulePath, rangeBase, rangeSize,
                                 _currentRelativeVirtualAddress)) {
        return false;
      }
      _rangeStart = rangeBase;

      _rangeLimit = rangeBase + rangeSize;
      _addressAdjustment = address - _currentRelativeVirtualAddress;
      if (_currentModulePath != newModulePath) {
        _currentModulePath = newModulePath;
        _currentModuleImage =
            _moduleDirectory.GetModuleImage(_currentModulePath);
        _currentVirtualAddressMap =
            (_currentModuleImage != nullptr)
                ? &(_currentModuleImage->GetVirtualAddressMap())
                : nullptr;
        _currentReader.reset((_currentVirtualAddressMap != nullptr) ? new
                                 typename VirtualAddressMap<Offset>::Reader(
                                     *_currentVirtualAddressMap)
                                                                    : nullptr);
      }
    } else {
      _currentRelativeVirtualAddress = address - _addressAdjustment;
    }

    return (_currentVirtualAddressMap != nullptr);
  }
};
}  // namespace chap
