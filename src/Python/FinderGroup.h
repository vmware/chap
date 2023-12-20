// Copyright (c) 2020-2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../CompoundDescriber.h"
#include "../ModuleDirectory.h"
#include "../UnfilledImages.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"
#include "ArenaDescriber.h"
#include "BlockAllocationFinder.h"
#include "InfrastructureFinder.h"
#include "TypeDirectory.h"

namespace chap {
namespace Python {
template <class Offset>
class FinderGroup {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;

  FinderGroup(VirtualMemoryPartition<Offset>& virtualMemoryPartition,
              const ModuleDirectory<Offset>& moduleDirectory,
              Allocations::Directory<Offset>& allocationDirectory,
              UnfilledImages<Offset>& unfilledImages)
      : _virtualMemoryPartition(virtualMemoryPartition),
        _virtualAddressMap(virtualMemoryPartition.GetAddressMap()),
        _moduleDirectory(moduleDirectory),
        _allocationDirectory(allocationDirectory),
        _unfilledImages(unfilledImages),
        _typeDirectory(_virtualAddressMap),
        _infrastructureFinder(moduleDirectory, virtualMemoryPartition,
                              _typeDirectory) {}

  void Resolve() {
    _infrastructureFinder.Resolve();
    if (_infrastructureFinder.ArenaStructArray() != 0) {
      // If we have arenas we need at least to find the fixed size blocks.
      _blockAllocationFinder.reset(new BlockAllocationFinder<Offset>(
          _virtualAddressMap, _infrastructureFinder));
      _allocationDirectory.AddFinder(_blockAllocationFinder.get());
      _arenaDescriber.reset(new ArenaDescriber<Offset>(_infrastructureFinder,
                                                       _virtualAddressMap));
    }
  }

  const InfrastructureFinder<Offset>& GetInfrastructureFinder() const {
    return _infrastructureFinder;
  }

  void AddDescribers(CompoundDescriber<Offset>& compoundDescriber) const {
    if (!(_arenaDescriber == nullptr)) {
      compoundDescriber.AddDescriber(*(_arenaDescriber.get()));
    }
  }

  void ClaimArenaRangesIfNeeded() {
    _infrastructureFinder.ClaimArenaRangesIfNeeded();
  }

 private:
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  const ModuleDirectory<Offset>& _moduleDirectory;
  Allocations::Directory<Offset>& _allocationDirectory;
  UnfilledImages<Offset>& _unfilledImages;
  TypeDirectory<Offset> _typeDirectory;
  InfrastructureFinder<Offset> _infrastructureFinder;
  std::unique_ptr<BlockAllocationFinder<Offset> > _blockAllocationFinder;
  std::unique_ptr<ArenaDescriber<Offset> > _arenaDescriber;
};

}  // namespace Python
}  // namespace chap
