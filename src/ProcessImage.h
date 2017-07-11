// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Allocations/Finder.h"
#include "Allocations/Graph.h"
#include "Allocations/SignatureDirectory.h"
#include "ModuleDirectory.h"
#include "ThreadMap.h"
#include "VirtualAddressMap.h"
#include "VirtualMemoryPartition.h"
namespace chap {
template <typename OffsetType>
class ProcessImage {
 public:
  typedef OffsetType Offset;
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef typename AddressMap::Reader Reader;
  typedef typename AddressMap::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  ProcessImage(const AddressMap &virtualAddressMap,
               const ThreadMap<Offset> &threadMap)
      : STACK_AREA("stack area"),
        _virtualAddressMap(virtualAddressMap),
        _threadMap(threadMap),
        _virtualMemoryPartition(virtualAddressMap),
        _lazyAllocationFinderInitializationPending(true),
        _lazyAllocationGraphInitializationPending(true),
        _unrecognizedMemoryAllocator(false),
        _allocationFinder((Allocations::Finder<Offset> *)(0)),
        _allocationGraph((Allocations::Graph<Offset> *)(0)) {
    for (typename ThreadMap<Offset>::const_iterator it = _threadMap.begin();
         it != _threadMap.end(); ++it) {
      _virtualMemoryPartition.ClaimRange(
          it->_stackBase, it->_stackLimit - it->_stackBase, STACK_AREA);
    }
  }

  virtual ~ProcessImage() {
    if (_allocationGraph != (Allocations::Graph<Offset> *)(0)) {
      delete _allocationGraph;
    }
    if (_allocationFinder != (Allocations::Finder<Offset> *)(0)) {
      delete _allocationFinder;
    }
  }
  const AddressMap &GetVirtualAddressMap() const { return _virtualAddressMap; }

  const ThreadMap<Offset> &GetThreadMap() const { return _threadMap; }

  const ModuleDirectory<Offset> &GetModuleDirectory() const {
    return _moduleDirectory;
  }

  const Allocations::SignatureDirectory<Offset> &GetSignatureDirectory() const {
    return _signatureDirectory;
  }

  Allocations::SignatureDirectory<Offset> &GetSignatureDirectory() {
    return _signatureDirectory;
  }

  const Allocations::Finder<Offset> *GetAllocationFinder() const {
    if (_lazyAllocationFinderInitializationPending) {
      _lazyAllocationFinderInitializationPending = false;
      MakeAllocationFinder();
    }
    RefreshSignatureDirectory();
    return _allocationFinder;
  }
  const Allocations::Graph<Offset> *GetAllocationGraph() const {
    if (_lazyAllocationFinderInitializationPending) {
      _lazyAllocationFinderInitializationPending = false;
      MakeAllocationFinder();
    }
    if ((_allocationFinder != (Allocations::Finder<Offset> *)(0)) &&
        _lazyAllocationGraphInitializationPending) {
      _lazyAllocationGraphInitializationPending = false;
      MakeAllocationGraph();
    }
    return _allocationGraph;
  }
  const char *STACK_AREA;

 protected:
  virtual void MakeAllocationFinder() const = 0;
  virtual void MakeAllocationGraph() const = 0;
  virtual void RefreshSignatureDirectory() const {}

  const AddressMap &_virtualAddressMap;
  const ThreadMap<OffsetType> &_threadMap;
  ModuleDirectory<Offset> _moduleDirectory;
  /*
   * The following are mutable because they are modified when the allocation
   * finder is made lazily.
   */
  mutable VirtualMemoryPartition<Offset> _virtualMemoryPartition;
  mutable bool _lazyAllocationFinderInitializationPending;
  mutable bool _lazyAllocationGraphInitializationPending;
  mutable bool _unrecognizedMemoryAllocator;
  mutable Allocations::Finder<Offset> *_allocationFinder;
  mutable Allocations::Graph<Offset> *_allocationGraph;
  mutable Allocations::SignatureDirectory<Offset> _signatureDirectory;
};
}  // namespace chap
