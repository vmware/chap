// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Allocations/AnchorDirectory.h"
#include "Allocations/Finder.h"
#include "Allocations/Graph.h"
#include "Allocations/SignatureDirectory.h"
#include "Allocations/TagHolder.h"
#include "COWStringAllocationsTagger.h"
#include "DequeAllocationsTagger.h"
#include "ListAllocationsTagger.h"
#include "LongStringAllocationsTagger.h"
#include "MapOrSetAllocationsTagger.h"
#include "ModuleDirectory.h"
#include "OpenSSLAllocationsTagger.h"
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
        _allocationFinder(nullptr),
        _allocationTagHolder(nullptr),
        _allocationGraph(nullptr),
        _unorderedMapOrSetAllocationsTagger(nullptr),
        _mapOrSetAllocationsTagger(nullptr),
        _dequeAllocationsTagger(nullptr),
        _listAllocationsTagger(nullptr),
        _longStringAllocationsTagger(nullptr),
        _vectorAllocationsTagger(nullptr),
        _cowStringAllocationsTagger(nullptr),
        _openSSLAllocationsTagger(nullptr) {
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
    if (_allocationFinder != nullptr) {
      delete _allocationFinder;
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

  const Allocations::Finder<Offset> *GetAllocationFinder() const {
    return _allocationFinder;
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

  const UnorderedMapOrSetAllocationsTagger<Offset>
      *GetUnorderedMapOrSetAllocationsTagger() const {
    return _unorderedMapOrSetAllocationsTagger;
  }

  const MapOrSetAllocationsTagger<Offset> *GetMapOrSetAllocationsTagger()
      const {
    return _mapOrSetAllocationsTagger;
  }

  const DequeAllocationsTagger<Offset> *GetDequeAllocationsTagger() const {
    return _dequeAllocationsTagger;
  }

  const ListAllocationsTagger<Offset> *GetListAllocationsTagger() const {
    return _listAllocationsTagger;
  }

  const LongStringAllocationsTagger<Offset> *GetLongStringAllocationsTagger()
      const {
    return _longStringAllocationsTagger;
  }

  const VectorAllocationsTagger<Offset> *GetVectorAllocationsTagger() const {
    return _vectorAllocationsTagger;
  }

  const COWStringAllocationsTagger<Offset> *GetCOWStringAllocationsTagger()
      const {
    return _cowStringAllocationsTagger;
  }

  const OpenSSLAllocationsTagger<Offset> *GetOpenSSLAllocationsTagger() const {
    return _openSSLAllocationsTagger;
  }

  const char *STACK;
  const char *STACK_OVERFLOW_GUARD;

 protected:
  const AddressMap &_virtualAddressMap;
  const ThreadMap<OffsetType> &_threadMap;
  VirtualMemoryPartition<Offset> _virtualMemoryPartition;
  ModuleDirectory<Offset> _moduleDirectory;
  UnfilledImages<Offset> _unfilledImages;
  Allocations::Finder<Offset> *_allocationFinder;
  Allocations::TagHolder<Offset> *_allocationTagHolder;
  Allocations::Graph<Offset> *_allocationGraph;
  Allocations::SignatureDirectory<Offset> _signatureDirectory;
  Allocations::AnchorDirectory<Offset> _anchorDirectory;
  UnorderedMapOrSetAllocationsTagger<Offset>
      *_unorderedMapOrSetAllocationsTagger;
  MapOrSetAllocationsTagger<Offset> *_mapOrSetAllocationsTagger;
  DequeAllocationsTagger<Offset> *_dequeAllocationsTagger;
  ListAllocationsTagger<Offset> *_listAllocationsTagger;
  LongStringAllocationsTagger<Offset> *_longStringAllocationsTagger;
  VectorAllocationsTagger<Offset> *_vectorAllocationsTagger;
  COWStringAllocationsTagger<Offset> *_cowStringAllocationsTagger;
  OpenSSLAllocationsTagger<Offset> *_openSSLAllocationsTagger;
};
}  // namespace chap
