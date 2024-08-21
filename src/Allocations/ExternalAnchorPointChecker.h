// Copyright (c) 2017,2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../VirtualAddressMap.h"
#include "Directory.h"
namespace chap {
namespace Allocations {
template <class Offset>
class ExternalAnchorPointChecker {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;

  ExternalAnchorPointChecker(const Directory<Offset>& directory,
                             const VirtualAddressMap<Offset>& addressMap)
      : _directory(directory), _addressMap(addressMap) {}

  virtual ~ExternalAnchorPointChecker() {}
  virtual const char* GetExternalAnchorReason(
      AllocationIndex index,
      const ContiguousImage<Offset>& contiguousImage) const = 0;

  const Directory<Offset>& GetAllocationDirectory() const { return _directory; }

  const VirtualAddressMap<Offset>& GetAddressMap() const { return _addressMap; }

 protected:
  const Directory<Offset>& _directory;
  const VirtualAddressMap<Offset>& _addressMap;
};
}  // namespace Allocations
}  // namespace chap
