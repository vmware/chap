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
  typedef typename Finder<Offset>::Allocation Allocation;

  ExternalAnchorPointChecker(const Finder<Offset>& allocationFinder)
      : _allocationFinder(allocationFinder),
        _addressMap(allocationFinder.GetAddressMap()) {}

  virtual ~ExternalAnchorPointChecker() {}
  virtual const char* GetExternalAnchorReason(
      AllocationIndex index,
      const ContiguousImage<Offset>& contiguousImage) const = 0;

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
