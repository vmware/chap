// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../CompoundDescriber.h"
#include "../ModuleDirectory.h"
#include "../UnfilledImages.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"
#include "CorruptionSkipper.h"
#include "DoublyLinkedListCorruptionChecker.h"
#include "FastBinFreeStatusFixer.h"
#include "HeapAllocationFinder.h"
#include "HeapDescriber.h"
#include "InfrastructureFinder.h"
#include "MainArenaAllocationFinder.h"
#include "MainArenaRunDescriber.h"
#include "MmappedAllocationDescriber.h"
#include "MmappedAllocationFinder.h"
// ??? logic for detecting thread cacheing

namespace chap {
namespace LibcMalloc {
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
        _infrastructureFinder(virtualMemoryPartition, moduleDirectory,
                              unfilledImages),
        _corruptionSkipper(_virtualAddressMap, _infrastructureFinder),
        _fastBinFreeStatusFixer(_virtualAddressMap, _infrastructureFinder,
                                allocationDirectory),
        _doublyLinkedListCorruptionChecker(
            _virtualAddressMap, _infrastructureFinder, allocationDirectory) {
    if (!(_infrastructureFinder.GetArenas().empty())) {
      // Add the finders that depend on arenas.

      _mainArenaAllocationFinder.reset(new MainArenaAllocationFinder<Offset>(
          _virtualAddressMap, _infrastructureFinder, _corruptionSkipper,
          _fastBinFreeStatusFixer, _doublyLinkedListCorruptionChecker));
      _allocationDirectory.AddFinder(_mainArenaAllocationFinder.get());
      if (!(_infrastructureFinder.GetHeaps().empty())) {
        _heapAllocationFinder.reset(new HeapAllocationFinder<Offset>(
            _virtualAddressMap, _infrastructureFinder, _corruptionSkipper,
            _fastBinFreeStatusFixer, _doublyLinkedListCorruptionChecker));
        _allocationDirectory.AddFinder(_heapAllocationFinder.get());
      }
    }
    /*
     * Finding mmapped() allocations used for libc does not depend
     * on finding any arenas.  It is possible, but a rather obscure case,
     * that malloc may have only been called on allocation sizes that
     * are large enough that they get mmapped.  In such a case there
     * may be mmapped allocations even when there are no detectable
     * arenas.
     */
    _mmappedAllocationFinder.reset(
        new MmappedAllocationFinder<Offset>(_virtualMemoryPartition));
    _allocationDirectory.AddFinder(_mmappedAllocationFinder.get());
  }

  const InfrastructureFinder<Offset>& GetInfrastructureFinder() const {
    return _infrastructureFinder;
  }

  void AddDescribers(CompoundDescriber<Offset>& compoundDescriber) {
    _heapDescriber.reset(
        new HeapDescriber<Offset>(_infrastructureFinder, _virtualAddressMap));
    _mainArenaRunDescriber.reset(
        new MainArenaRunDescriber<Offset>(_infrastructureFinder));
    _mmappedAllocationDescriber.reset(new MmappedAllocationDescriber<Offset>(
        _mmappedAllocationFinder->GetMmappedChunks()));
    compoundDescriber.AddDescriber(*(_heapDescriber.get()));
    compoundDescriber.AddDescriber(*(_mainArenaRunDescriber.get()));
    compoundDescriber.AddDescriber(*(_mmappedAllocationDescriber.get()));
  }

 private:
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  const ModuleDirectory<Offset>& _moduleDirectory;
  Allocations::Directory<Offset>& _allocationDirectory;
  UnfilledImages<Offset>& _unfilledImages;
  InfrastructureFinder<Offset> _infrastructureFinder;
  CorruptionSkipper<Offset> _corruptionSkipper;
  FastBinFreeStatusFixer<Offset> _fastBinFreeStatusFixer;
  DoublyLinkedListCorruptionChecker<Offset> _doublyLinkedListCorruptionChecker;
  std::unique_ptr<MmappedAllocationFinder<Offset> > _mmappedAllocationFinder;
  std::unique_ptr<HeapAllocationFinder<Offset> > _heapAllocationFinder;
  std::unique_ptr<MainArenaAllocationFinder<Offset> >
      _mainArenaAllocationFinder;
  std::unique_ptr<HeapDescriber<Offset> > _heapDescriber;
  std::unique_ptr<MainArenaRunDescriber<Offset> > _mainArenaRunDescriber;
  std::unique_ptr<MmappedAllocationDescriber<Offset> >
      _mmappedAllocationDescriber;
};

}  // namespace LibcMalloc
}  // namespace chap
