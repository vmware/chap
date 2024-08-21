// Copyright (c) 2017-2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Allocations/AnchorDirectory.h"
#include "Allocations/Directory.h"
#include "Allocations/EdgePredicate.h"
#include "Allocations/Graph.h"
#include "Allocations/SignatureDirectory.h"
#include "Allocations/TagHolder.h"
#include "CPlusPlus/COWStringAllocationsTagger.h"
#include "CPlusPlus/DequeAllocationsTagger.h"
#include "CPlusPlus/ListAllocationsTagger.h"
#include "CPlusPlus/LongStringAllocationsTagger.h"
#include "CPlusPlus/MapOrSetAllocationsTagger.h"
#include "CPlusPlus/TypeInfoDirectory.h"
#include "CPlusPlus/UnorderedMapOrSetAllocationsTagger.h"
#include "CPlusPlus/VectorAllocationsTagger.h"
#include "FileMappedRangeDirectory.h"
#include "FollyFibers/InfrastructureFinder.h"
#include "GoLang/AllocationsTagger.h"
#include "GoLang/FinderGroup.h"
#include "ModuleDirectory.h"
#include "ModuleImageFactory.h"
#include "OpenSSLAllocationsTagger.h"
#include "PThread/InfrastructureFinder.h"
#include "Python/AllocationsTagger.h"
#include "Python/FinderGroup.h"
#include "Python/InfrastructureFinder.h"
#include "StackRegistry.h"
#include "TCMalloc/FinderGroup.h"
#include "ThreadMap.h"
#include "UnfilledImages.h"
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
               const ThreadMap<Offset> &threadMap,
               ModuleImageFactory<Offset> *moduleImageFactory)
      : STACK("stack"),
        STACK_OVERFLOW_GUARD("stack overflow guard"),
        _virtualAddressMap(virtualAddressMap),
        _threadMap(threadMap),
        _virtualMemoryPartition(virtualAddressMap),
        _fileMappedRangeDirectory(_virtualMemoryPartition),
        _moduleDirectory(_virtualMemoryPartition, moduleImageFactory),
        _unfilledImages(virtualAddressMap),
        _allocationTagHolder(nullptr),
        _allocationGraph(nullptr),
        _pythonFinderGroup(_virtualMemoryPartition, _moduleDirectory,
                           _allocationDirectory, _unfilledImages),
        _goLangFinderGroup(_virtualMemoryPartition, _moduleDirectory,
                           _allocationDirectory, _unfilledImages,
                           _stackRegistry),
        _TCMallocFinderGroup(_virtualMemoryPartition, _moduleDirectory,
                             _allocationDirectory, _unfilledImages),
        _pThreadInfrastructureFinder(_moduleDirectory, _virtualMemoryPartition,
                                     _stackRegistry),
        _follyFibersInfrastructureFinder(
            _moduleDirectory, _virtualMemoryPartition, _stackRegistry),
        _typeInfoDirectory(_moduleDirectory, _virtualAddressMap,
                           _allocationDirectory) {}

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

  const StackRegistry<Offset> &GetStackRegistry() const {
    return _stackRegistry;
  }

  const ModuleDirectory<Offset> &GetFileMappedRangeDirectory() const {
    return _fileMappedRangeDirectory;
  }

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

  const Allocations::EdgePredicate<Offset> *GetEdgeIsTainted() const {
    return _edgeIsTainted;
  }

  const Allocations::EdgePredicate<Offset> *GetEdgeIsFavored() const {
    return _edgeIsFavored;
  }

  const PThread::InfrastructureFinder<Offset> &GetPThreadInfrastructureFinder()
      const {
    return _pThreadInfrastructureFinder;
  }

  const FollyFibers::InfrastructureFinder<Offset>
      &FollyFibersInfrastructureFinder() const {
    return _follyFibersInfrastructureFinder;
  }

  const Python::InfrastructureFinder<Offset> &GetPythonInfrastructureFinder()
      const {
    return _pythonFinderGroup.GetInfrastructureFinder();
  }

  const Python::FinderGroup<Offset> &GetPythonFinderGroup() const {
    return _pythonFinderGroup;
  }

  const CPlusPlus::TypeInfoDirectory<Offset> &GetTypeInfoDirectory() const {
    return _typeInfoDirectory;
  }

  const char *STACK;
  const char *STACK_OVERFLOW_GUARD;

 protected:
  const AddressMap &_virtualAddressMap;
  Allocations::Directory<Offset> _allocationDirectory;
  const ThreadMap<OffsetType> &_threadMap;
  StackRegistry<Offset> _stackRegistry;
  VirtualMemoryPartition<Offset> _virtualMemoryPartition;
  FileMappedRangeDirectory<Offset> _fileMappedRangeDirectory;
  ModuleDirectory<Offset> _moduleDirectory;
  UnfilledImages<Offset> _unfilledImages;
  Allocations::TagHolder<Offset> *_allocationTagHolder;
  Allocations::EdgePredicate<Offset> *_edgeIsTainted;
  Allocations::EdgePredicate<Offset> *_edgeIsFavored;
  Allocations::Graph<Offset> *_allocationGraph;
  Allocations::SignatureDirectory<Offset> _signatureDirectory;
  Allocations::AnchorDirectory<Offset> _anchorDirectory;
  Python::FinderGroup<Offset> _pythonFinderGroup;
  GoLang::FinderGroup<Offset> _goLangFinderGroup;
  TCMalloc::FinderGroup<Offset> _TCMallocFinderGroup;
  PThread::InfrastructureFinder<Offset> _pThreadInfrastructureFinder;
  FollyFibers::InfrastructureFinder<Offset> _follyFibersInfrastructureFinder;
  CPlusPlus::TypeInfoDirectory<Offset> _typeInfoDirectory;

  /*
   * Pre-tag all allocations.  This should be done just once, at the end
   * of the constructor for the derived class.
   */
  void TagAllocations() {
    _edgeIsTainted =
        new Allocations::EdgePredicate<Offset>(*_allocationGraph, false);

    _edgeIsFavored =
        new Allocations::EdgePredicate<Offset>(*_allocationGraph, false);

    _allocationTagHolder = new Allocations::TagHolder<Offset>(
        _allocationDirectory.NumAllocations(), *_edgeIsFavored,
        *_edgeIsTainted);

    Allocations::TaggerRunner<Offset> runner(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        _signatureDirectory);

    runner.RegisterTagger(
        new CPlusPlus::UnorderedMapOrSetAllocationsTagger<Offset>(
            *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
            *_edgeIsFavored));

    runner.RegisterTagger(new CPlusPlus::MapOrSetAllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        *_edgeIsFavored));

    runner.RegisterTagger(new CPlusPlus::DequeAllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        *_edgeIsFavored));

    runner.RegisterTagger(new CPlusPlus::ListAllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        *_edgeIsFavored));

    runner.RegisterTagger(new CPlusPlus::LongStringAllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        *_edgeIsFavored, _moduleDirectory, _signatureDirectory));

    runner.RegisterTagger(new CPlusPlus::VectorAllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        *_edgeIsFavored, _signatureDirectory));

    runner.RegisterTagger(new CPlusPlus::COWStringAllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        *_edgeIsFavored, _moduleDirectory));

    runner.RegisterTagger(new OpenSSLAllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsFavored,
        _moduleDirectory, _virtualAddressMap));

    runner.RegisterTagger(new Python::AllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        *_edgeIsFavored, _pythonFinderGroup.GetInfrastructureFinder(),
        _virtualAddressMap));

    runner.RegisterTagger(new GoLang::AllocationsTagger<Offset>(
        *_allocationGraph, *_allocationTagHolder, *_edgeIsTainted,
        *_edgeIsFavored, _goLangFinderGroup.GetInfrastructureFinder(),
        _goLangFinderGroup.GetMappedPageRangeAllocationFinderIndex(),
        _virtualAddressMap));

    runner.ResolveAllAllocationTags();
  }
};
}  // namespace chap
