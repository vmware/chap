// Copyright (c) 2020-2021,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../CompoundDescriber.h"
#include "../ModuleDirectory.h"
#include "../StackRegistry.h"
#include "../UnfilledImages.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"
#include "InfrastructureFinder.h"
#include "MappedPageRangeAllocationFinder.h"

namespace chap {
namespace GoLang {
template <class Offset>
class FinderGroup {
 public:
  FinderGroup(VirtualMemoryPartition<Offset>& virtualMemoryPartition,
              const ModuleDirectory<Offset>& moduleDirectory,
              Allocations::Directory<Offset>& allocationDirectory,
              UnfilledImages<Offset>& unfilledImages,
              StackRegistry<Offset>& stackRegistry)
      : _virtualMemoryPartition(virtualMemoryPartition),
        _virtualAddressMap(virtualMemoryPartition.GetAddressMap()),
        _moduleDirectory(moduleDirectory),
        _allocationDirectory(allocationDirectory),
        _unfilledImages(unfilledImages),
        _infrastructureFinder(moduleDirectory, virtualMemoryPartition,
                              stackRegistry),
        _mappedPageRangeAllocationFinderIndex(~0) {}

  void Resolve() {
    _infrastructureFinder.Resolve();
    if (_infrastructureFinder.FoundRangesAndSizes()) {
      _mappedPageRangeAllocationFinder.reset(
          new MappedPageRangeAllocationFinder<Offset>(
              _virtualAddressMap, _infrastructureFinder, _allocationDirectory));
      _mappedPageRangeAllocationFinderIndex = _allocationDirectory.AddFinder(
          _mappedPageRangeAllocationFinder.get());
      // TODO: create any region describers
    }
  }

  const InfrastructureFinder<Offset>& GetInfrastructureFinder() const {
    return _infrastructureFinder;
  }

  size_t GetMappedPageRangeAllocationFinderIndex() const {
    return _mappedPageRangeAllocationFinderIndex;
  }

  void AddDescribers(CompoundDescriber<Offset>& /* compoundDescriber */) const {
    // Probably there should be at least a describer for a span (as a
    // large memory range.
    // Various other objects that are not considered part of allocations
    // should be describeed as well.
  }

 private:
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  const ModuleDirectory<Offset>& _moduleDirectory;
  Allocations::Directory<Offset>& _allocationDirectory;
  UnfilledImages<Offset>& _unfilledImages;
  InfrastructureFinder<Offset> _infrastructureFinder;
  std::unique_ptr<MappedPageRangeAllocationFinder<Offset> >
      _mappedPageRangeAllocationFinder;
  size_t _mappedPageRangeAllocationFinderIndex;
};

}  // namespace GoLang
}  // namespace chap
