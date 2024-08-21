// Copyright (c) 2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <memory>
#include "Directory.h"
#include "Set.h"
namespace chap {
namespace Allocations {
template <class Offset>
class SetCache {
 public:
  SetCache(typename Directory<Offset>::AllocationIndex numAllocations)
      : _numAllocations(numAllocations),
        _visited(numAllocations),
        _derived(numAllocations) {}
  Set<Offset>& GetVisited() { return _visited; }
  Set<Offset>& GetDerived() { return _derived; }
  const Set<Offset>& GetDerived() const { return _derived; }

 private:
  typename Directory<Offset>::AllocationIndex _numAllocations;
  Set<Offset> _visited;
  Set<Offset> _derived;
};
}  // namespace Allocations
}  // namespace chap
