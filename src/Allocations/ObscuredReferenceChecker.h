// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Directory.h"
namespace chap {
namespace Allocations {
template <class Offset>
class ObscuredReferenceChecker {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;

  ObscuredReferenceChecker() {}

  virtual ~ObscuredReferenceChecker() {}

  /*
   * Return a valid AllocationIndex if the given address can be interpreted
   * as a reference to an allocation, or _directory.NumAllocations() otherwise.
   */
  virtual AllocationIndex AllocationIndexOf(Offset addr) const = 0;
};
}  // namespace Allocations
}  // namespace chap
