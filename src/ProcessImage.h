// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Allocations/AnchorDirectory.h"
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

  const Allocations::AnchorDirectory<Offset> &GetAnchorDirectory() const {
    return _anchorDirectory;
  }

  Allocations::AnchorDirectory<Offset> &GetAnchorDirectory() {
    return _anchorDirectory;
  }

  const Allocations::Finder<Offset> *GetAllocationFinder() const {
    RefreshSignatureDirectory();
    return _allocationFinder;
  }
  const Allocations::Graph<Offset> *GetAllocationGraph() const {
    return _allocationGraph;
  }
  const char *STACK_AREA;

 protected:
  virtual void RefreshSignatureDirectory() const {}

  const AddressMap &_virtualAddressMap;
  const ThreadMap<OffsetType> &_threadMap;
  ModuleDirectory<Offset> _moduleDirectory;
  VirtualMemoryPartition<Offset> _virtualMemoryPartition;
  Allocations::Finder<Offset> *_allocationFinder;
  Allocations::Graph<Offset> *_allocationGraph;

  /*
   * At present this is mutable because some const functions cause these
   * directories to be refreshed (because we are allowing the
   * user to make a symdefs file between commands).
   */
  mutable Allocations::SignatureDirectory<Offset> _signatureDirectory;
  mutable Allocations::AnchorDirectory<Offset> _anchorDirectory;
};
}  // namespace chap
