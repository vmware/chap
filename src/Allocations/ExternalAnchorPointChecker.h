// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Finder.h"
namespace chap {
namespace Allocations {
template <class Offset>
class ExternalAnchorPointChecker {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::AllocationImage AllocationImage;
  typedef typename Finder<Offset>::Allocation Allocation;

  ExternalAnchorPointChecker(const Finder<Offset>& allocationFinder)
      : _allocationFinder(allocationFinder),
        _addressMap(allocationFinder.GetAddressMap()) {}

  virtual ~ExternalAnchorPointChecker() {}
  const char* GetExternalAnchorReason(AllocationIndex index) const {
    const Allocation* allocation = _allocationFinder.AllocationAt(index);
    if (allocation != (const Allocation*)0 && allocation->IsUsed()) {
      AllocationImage allocationImage(_addressMap, *allocation);
      return ExternalAnchorReason(allocationImage, allocation->Size());
    }
    return (const char*)0;
  }

  virtual const char* ExternalAnchorReason(AllocationImage& image,
                                           Offset size) const = 0;

  const Finder<Offset>& GetAllocationFinder() const {
    return _allocationFinder;
  }

  const VirtualAddressMap<Offset>& GetAddressMap() const { return _addressMap; }

 protected:
  const Finder<Offset>& _allocationFinder;
  const VirtualAddressMap<Offset>& _addressMap;
};
}  // namespace Allocations
}  // namespace chap
