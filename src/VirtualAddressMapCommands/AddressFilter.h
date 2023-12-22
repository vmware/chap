// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../ProcessImage.h"
#include "../VirtualAddressMap.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class AddressFilter {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  AddressFilter(const ProcessImage<Offset>& processImage,
                Commands::Context& context)
      : _processImage(processImage),
        _addressMap(processImage.GetVirtualAddressMap()),
        _allocationDirectory(processImage.GetAllocationDirectory()),
        _stackRegistry(processImage.GetStackRegistry()),
        _isActive(false),
        _hasErrors(false),
        _skipFree(false),
        _skipUsed(false),
        _skipAllocations(false),
        _skipStacks(false),
        _numAllocations(_allocationDirectory.NumAllocations()) {
    size_t numSkipArguments = context.GetNumArguments("skip");
    Commands::Error& error = context.GetError();
    if (numSkipArguments != 0) {
      for (size_t i = 0; i < numSkipArguments; i++) {
        const std::string toSkip = context.Argument("skip", i);
        if (toSkip == "free") {
          _skipFree = true;
          _isActive = true;
          continue;
        }
        if (toSkip == "used") {
          _skipUsed = true;
          _isActive = true;
          continue;
        }
        if (toSkip == "allocations") {
          _skipAllocations = true;
          _skipUsed = true;
          _skipFree = true;
          _isActive = true;
          continue;
        }
        if (toSkip == "stacks") {
          _skipStacks = true;
          _isActive = true;
          continue;
        }
        error << "Skipping \"" << toSkip << "\" is not supported.\n";
        _hasErrors = true;
      }
    }
  }

  bool HasErrors() const { return _hasErrors; }
  bool IsActive() const { return _isActive; }
  bool Exclude(Offset address) const {
    if (!_isActive) {
      return false;
    }
    if (_skipAllocations || _skipFree || _skipUsed) {
      AllocationIndex index = _allocationDirectory.AllocationIndexOf(address);
      if (index != _numAllocations) {
        if (_skipAllocations) {
          return true;
        }
        if (_allocationDirectory.AllocationAt(index)->IsUsed()) {
          if (_skipUsed) {
            return true;
          }
        } else {
          if (_skipFree) {
            return true;
          }
        }
      }
    }
    if (_skipStacks && _stackRegistry.IsStackAddress(address)) {
      return true;
    }
    return false;
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const VirtualAddressMap<Offset>& _addressMap;
  const Allocations::Directory<Offset>& _allocationDirectory;
  const StackRegistry<Offset>& _stackRegistry;
  bool _isActive;
  bool _hasErrors;
  bool _skipFree;
  bool _skipUsed;
  bool _skipAllocations;
  bool _skipStacks;
  AllocationIndex _numAllocations;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
