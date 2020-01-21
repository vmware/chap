// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Finder.h"
namespace chap {
namespace Allocations {
template <class Offset>
class ObscuredReferenceChecker {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;

  ObscuredReferenceChecker(const Finder<Offset>& allocationFinder)
      : _allocationFinder(allocationFinder) {}

  virtual ~ObscuredReferenceChecker() {}

  /*
   * Return a valid AllocationIndex if the given address can be interpreted
   * as a reference to an allocation, or _finder.NumAllocations() otherwise.
   */
  virtual AllocationIndex AllocationIndexOf(Offset addr) const = 0;

 protected:
  const Finder<Offset>& _allocationFinder;
};
}  // namespace Allocations
}  // namespace chap
