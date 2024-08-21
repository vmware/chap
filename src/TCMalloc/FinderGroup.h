// Copyright (c) 2024 Broadcom. All Rights Reserved.
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
#include "PageMapAllocationFinder.h"

namespace chap {
namespace TCMalloc {
template <class Offset>
class FinderGroup {
 public:
  FinderGroup(VirtualMemoryPartition<Offset>& virtualMemoryPartition,
              const ModuleDirectory<Offset>& moduleDirectory,
              Allocations::Directory<Offset>& allocationDirectory,
              UnfilledImages<Offset>& unfilledImages)
      : _virtualMemoryPartition(virtualMemoryPartition),
        _virtualAddressMap(virtualMemoryPartition.GetAddressMap()),
        _moduleDirectory(moduleDirectory),
        _allocationDirectory(allocationDirectory),
        _unfilledImages(unfilledImages),
        _infrastructureFinder(virtualMemoryPartition,moduleDirectory, 
                              _unfilledImages) {}

  void Resolve() {
    _infrastructureFinder.Resolve();
    Offset pageMap = _infrastructureFinder.GetPageMap();
    if (pageMap != 0) {
      _pageMapAllocationFinder.reset(new PageMapAllocationFinder<Offset>(
          _virtualAddressMap, _infrastructureFinder, _allocationDirectory));
      _allocationDirectory.AddFinder(_pageMapAllocationFinder.get());
      // add a page map describer ???
      // add leaf describer ???
      // add middle node describer ???
      // add span describer
    }
  }

  const InfrastructureFinder<Offset>& GetInfrastructureFinder() const {
    return _infrastructureFinder;
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
  std::unique_ptr<PageMapAllocationFinder<Offset> > _pageMapAllocationFinder;
};

}  // namespace TCMalloc
}  // namespace chap
