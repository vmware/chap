// Copyright (c) 2020-2021 VMware, Inc. All Rights Reserved.
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
                              stackRegistry) {}

  void Resolve() {
    _infrastructureFinder.Resolve();
    // ??? mheap_ was found
    //    Create a finder for allocations in spans
    //    Add that finder to the allocation directory.
    //    Create any region describers
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
};

}  // namespace GoLang
}  // namespace chap
