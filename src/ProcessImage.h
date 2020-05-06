// Copyright (c) 2017-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Allocations/AnchorDirectory.h"
#include "Allocations/Directory.h"
#include "Allocations/Graph.h"
#include "Allocations/SignatureDirectory.h"
#include "Allocations/TagHolder.h"
#include "COWStringAllocationsTagger.h"
#include "DequeAllocationsTagger.h"
#include "GoLang/FinderGroup.h"
#include "ListAllocationsTagger.h"
#include "LongStringAllocationsTagger.h"
#include "MapOrSetAllocationsTagger.h"
#include "ModuleDirectory.h"
#include "OpenSSLAllocationsTagger.h"
#include "Python/AllocationsTagger.h"
#include "Python/FinderGroup.h"
#include "Python/InfrastructureFinder.h"
#include "ThreadMap.h"
#include "UnfilledImages.h"
#include "UnorderedMapOrSetAllocationsTagger.h"
#include "VectorAllocationsTagger.h"
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
      : STACK("stack"),
        STACK_OVERFLOW_GUARD("stack overflow guard"),
        _virtualAddressMap(virtualAddressMap),
        _threadMap(threadMap),
        _virtualMemoryPartition(virtualAddressMap),
        _moduleDirectory(_virtualMemoryPartition),
        _unfilledImages(virtualAddressMap),
        _allocationTagHolder(nullptr),
        _allocationGraph(nullptr),
        _pythonFinderGroup(_virtualMemoryPartition, _moduleDirectory,
                           _allocationDirectory, _unfilledImages),
        _goLangFinderGroup(_virtualMemoryPartition, _moduleDirectory,
                           _allocationDirectory, _unfilledImages) {
    for (typename ThreadMap<Offset>::const_iterator it = _threadMap.begin();
         it != _threadMap.end(); ++it) {
      if (!_virtualMemoryPartition.ClaimRange(
              it->_stackBase, it->_stackLimit - it->_stackBase, STACK, false)) {
        std::cerr << "Warning: overlap found for stack range "
                  << "for thread " << std::dec << it->_threadNum << ".\n";
      }
    }
  }

  virtual ~ProcessImage() {
    if (_allocationGraph != nullptr) {
      delete _allocationGraph;
    }
    if (_allocationTagHolder != nullptr) {
      delete _allocationTagHolder;
    }
  }

  const AddressMap &GetVirtualAddressMap() const { return _virtualAddressMap; }

  const VirtualMemoryPartition<Offset> &GetVirtualMemoryPartition() const {
    return _virtualMemoryPartition;
  }

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

  const Allocations::Directory<Offset> &GetAllocationDirectory() const {
    return _allocationDirectory;
  }

  const Allocations::TagHolder<Offset> *GetAllocationTagHolder() const {
    return _allocationTagHolder;
  }

  Allocations::TagHolder<Offset> *GetAllocationTagHolder() {
    return _allocationTagHolder;
  }

  const Allocations::Graph<Offset> *GetAllocationGraph() const {
    return _allocationGraph;
  }

  const Python::InfrastructureFinder<Offset> &GetPythonInfrastructureFinder()
      const {
    return _pythonFinderGroup.GetInfrastructureFinder();
  }

  const Python::FinderGroup<Offset> &GetPythonFinderGroup() const {
    return _pythonFinderGroup;
  }

  const char *STACK;
  const char *STACK_OVERFLOW_GUARD;

 protected:
  const AddressMap &_virtualAddressMap;
  Allocations::Directory<Offset> _allocationDirectory;
  const ThreadMap<OffsetType> &_threadMap;
  VirtualMemoryPartition<Offset> _virtualMemoryPartition;
  ModuleDirectory<Offset> _moduleDirectory;
  UnfilledImages<Offset> _unfilledImages;
  Allocations::TagHolder<Offset> *_allocationTagHolder;
  Allocations::Graph<Offset> *_allocationGraph;
  Allocations::SignatureDirectory<Offset> _signatureDirectory;
  Allocations::AnchorDirectory<Offset> _anchorDirectory;
  Python::FinderGroup<Offset> _pythonFinderGroup;
  GoLang::FinderGroup<Offset> _goLangFinderGroup;

  /*
   * Pre-tag all allocations.  This should be done just once, at the end
   * of the constructor for the derived class.
   */
  void TagAllocations() {
    _allocationTagHolder = new Allocations::TagHolder<Offset>(
        _allocationDirectory.NumAllocations());

    Allocations::TaggerRunner<Offset> runner(
        *_allocationGraph, *_allocationTagHolder, _signatureDirectory);

    runner.RegisterTagger(new UnorderedMapOrSetAllocationsTagger<Offset>(
        *(_allocationGraph), *(_allocationTagHolder)));

    runner.RegisterTagger(new MapOrSetAllocationsTagger<Offset>(
        *(_allocationGraph), *(_allocationTagHolder)));

    runner.RegisterTagger(new DequeAllocationsTagger<Offset>(
        *(_allocationGraph), *(_allocationTagHolder)));

    runner.RegisterTagger(new ListAllocationsTagger<Offset>(
        *(_allocationGraph), *(_allocationTagHolder)));

    runner.RegisterTagger(new LongStringAllocationsTagger<Offset>(
        *(_allocationGraph), *(_allocationTagHolder), _moduleDirectory));

    runner.RegisterTagger(new VectorAllocationsTagger<Offset>(
        *(_allocationGraph), *(_allocationTagHolder)));

    runner.RegisterTagger(new COWStringAllocationsTagger<Offset>(
        *(_allocationGraph), *(_allocationTagHolder), _moduleDirectory));

    runner.RegisterTagger(new OpenSSLAllocationsTagger<Offset>(
        *(_allocationTagHolder), _moduleDirectory, _virtualAddressMap));

    runner.RegisterTagger(new Python::AllocationsTagger<Offset>(
        *(_allocationGraph), *(_allocationTagHolder),
        _pythonFinderGroup.GetInfrastructureFinder(), _virtualAddressMap));

    runner.ResolveAllAllocationTags();
  }
};
}  // namespace chap
