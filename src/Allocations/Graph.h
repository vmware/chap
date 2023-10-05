// Copyright (c) 2017,2020-2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <algorithm>
#include <deque>
#include "../StackRegistry.h"
#include "../ThreadMap.h"
#include "../VirtualAddressMap.h"
#include "ContiguousImage.h"
#include "Directory.h"
#include "ExternalAnchorPointChecker.h"
#include "IndexedDistances.h"
#include "ObscuredReferenceChecker.h"

namespace chap {
namespace Allocations {
template <class Offset>
class Graph {
 public:
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef typename Directory<Offset>::Allocation Allocation;
  typedef typename Directory<Offset>::AllocationIndex Index;
  typedef Offset EdgeIndex;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;

  class AnchorChainVisitor {
   public:
    virtual bool VisitStaticAnchorChainHeader(
        const std::vector<Offset> &staticAddrs, Offset address, Offset size,
        const char *image) = 0;
    virtual bool VisitStackAnchorChainHeader(
        const std::vector<Offset> &stackAddrs, Offset address, Offset size,
        const char *image) = 0;
    virtual bool VisitRegisterAnchorChainHeader(
        const std::vector<std::pair<size_t, const char *> > &anchors,
        Offset address, Offset size, const char *image) = 0;
    virtual bool VisitChainLink(Offset address, Offset size,
                                const char *image) = 0;
  };

  Graph(const VirtualAddressMap<Offset> &addressMap,
        const Directory<Offset> &directory, const ThreadMap<Offset> &threadMap,
        const StackRegistry<Offset> &stackRegistry,
        const std::map<Offset, Offset> &staticAnchorLimits,
        const ExternalAnchorPointChecker<Offset> *externalAnchorPointChecker,
        const ObscuredReferenceChecker<Offset> *obscuredReferenceChecker)
      : _directory(directory),
        _addressMap(addressMap),
        _threadMap(threadMap),
        _stackRegistry(stackRegistry),
        _externalAnchorPointChecker(externalAnchorPointChecker),
        _obscuredReferenceChecker(obscuredReferenceChecker),
        _numAllocations(directory.NumAllocations()),
        _totalEdges(0),
        _staticAnchorDistances(_numAllocations),
        _stackAnchorDistances(_numAllocations),
        _registerAnchorDistances(_numAllocations),
        _externalAnchorDistances(_numAllocations) {
    FindEdges();
    FindStaticAnchorPoints(staticAnchorLimits);
    FindStackAnchorPoints();
    FindRegisterAnchorPoints();
    FindExternalAnchorPoints();
    MarkLeakedChunks();
  }

  const Directory<Offset> &GetAllocationDirectory() const { return _directory; }

  const VirtualAddressMap<Offset> &GetAddressMap() const { return _addressMap; }

  EdgeIndex TotalEdges() const { return _totalEdges; }

  void GetIncoming(Index target, const Index **pFirstIncoming,
                   const Index **pPastIncoming) const {
    if (target < _numAllocations) {
      *pFirstIncoming = &(_incoming[_firstIncoming[target]]);
      *pPastIncoming = &(_incoming[_firstIncoming[target + 1]]);
    } else {
      *pFirstIncoming = (const Index *)(0);
      *pPastIncoming = (const Index *)(0);
    }
  }

  void GetIncoming(Index target, EdgeIndex &firstIncoming,
                   EdgeIndex &pastIncoming) const {
    if (target < _numAllocations) {
      firstIncoming = _firstIncoming[target];
      pastIncoming = _firstIncoming[target + 1];
    } else {
      firstIncoming = _totalEdges;
      pastIncoming = _totalEdges;
    }
  }

  EdgeIndex GetIncomingEdgeIndex(Index source, Index target) const {
    if (source >= _numAllocations || target >= _numAllocations) {
      return _totalEdges;
    }
    EdgeIndex base = _firstIncoming[target];
    EdgeIndex limit = _firstIncoming[target + 1];

    while (base < limit) {
      size_t mid = (base + limit) / 2;
      Index edgeSource = _incoming[mid];
      if (source >= edgeSource) {
        if (source == edgeSource) {
          return mid;
        } else {
          base = mid + 1;
        }
      } else {
        limit = mid;
      }
    }
    return _totalEdges;
  }

  Index GetSourceForIncoming(EdgeIndex incoming) const {
    return (incoming < _totalEdges) ? _incoming[incoming] : _numAllocations;
  }

  Index SourceAllocationIndex(Index target, Offset addr) const {
    if (target < _numAllocations) {
      EdgeIndex base = _firstIncoming[target];
      EdgeIndex limit = _firstIncoming[target + 1];
      while (base < limit) {
        size_t mid = (base + limit) / 2;
        Index source = _incoming[mid];
        const Allocation &allocation = *(_directory.AllocationAt(source));
        if (addr >= allocation.Address()) {
          if (addr < allocation.Address() + allocation.Size()) {
            return source;
          } else {
            base = mid + 1;
          }
        } else {
          limit = mid;
        }
      }
    }
    return _numAllocations;
  }

  void GetOutgoing(Index source, const Index **pFirstOutgoing,
                   const Index **pPastOutgoing) const {
    if (source < _numAllocations) {
      *pFirstOutgoing = &(_outgoing[_firstOutgoing[source]]);
      *pPastOutgoing = &(_outgoing[_firstOutgoing[source + 1]]);
    } else {
      *pFirstOutgoing = (const Index *)(0);
      *pPastOutgoing = (const Index *)(0);
    }
  }

  void GetOutgoing(Index source, EdgeIndex &firstOutgoing,
                   EdgeIndex &pastOutgoing) const {
    if (source < _numAllocations) {
      firstOutgoing = _firstOutgoing[source];
      pastOutgoing = _firstOutgoing[source + 1];
    } else {
      firstOutgoing = _totalEdges;
      pastOutgoing = _totalEdges;
    }
  }

  EdgeIndex GetOutgoingEdgeIndex(Index source, Index target) const {
    if (source >= _numAllocations || target >= _numAllocations) {
      return _totalEdges;
    }
    EdgeIndex base = _firstOutgoing[source];
    EdgeIndex limit = _firstOutgoing[source + 1];

    while (base < limit) {
      size_t mid = (base + limit) / 2;
      Index edgeTarget = _outgoing[mid];
      if (target >= edgeTarget) {
        if (target == edgeTarget) {
          return mid;
        } else {
          base = mid + 1;
        }
      } else {
        limit = mid;
      }
    }
    return _totalEdges;
  }

  Index GetTargetForOutgoing(EdgeIndex outgoing) const {
    return (outgoing < _totalEdges) ? _outgoing[outgoing] : _numAllocations;
  }

  bool HasNoOutgoing(Index source) const {
    return (source >= _numAllocations) ||
           (_firstOutgoing[source] == _firstOutgoing[source + 1]);
  }

  Index TargetAllocationIndex(Index source, Offset addr) const {
    if (source < _numAllocations) {
      EdgeIndex base = _firstOutgoing[source];
      EdgeIndex limit = _firstOutgoing[source + 1];
      while (base < limit) {
        size_t mid = (base + limit) / 2;
        Index target = _outgoing[mid];
        const Allocation &allocation = *(_directory.AllocationAt(target));
        if (addr >= allocation.Address()) {
          if (addr < allocation.Address() + allocation.Size()) {
            return target;
          } else {
            base = mid + 1;
          }
        } else {
          limit = mid;
        }
      }
    }
    return _numAllocations;
  }

  EdgeIndex TargetEdgeIndex(Index source, Offset addr) const {
    if (source < _numAllocations) {
      EdgeIndex base = _firstOutgoing[source];
      EdgeIndex limit = _firstOutgoing[source + 1];
      while (base < limit) {
        size_t mid = (base + limit) / 2;
        Index target = _outgoing[mid];
        const Allocation &allocation = *(_directory.AllocationAt(target));
        if (addr >= allocation.Address()) {
          if (addr < allocation.Address() + allocation.Size()) {
            return mid;
          } else {
            base = mid + 1;
          }
        } else {
          limit = mid;
        }
      }
    }
    return _totalEdges;
  }

  bool IsLeaked(Index index) const {
    return index < _numAllocations && _leaked[index];
  }

  bool IsAnchored(Index index) const {
    return (index < _numAllocations) &&
           _directory.AllocationAt(index)->IsUsed() && !_leaked[index];
  }

  bool IsAnchorPoint(Index index) const {
    return (index < _numAllocations) &&
           ((_staticAnchorDistances.GetDistance(index) == 1) ||
            (_stackAnchorDistances.GetDistance(index) == 1) ||
            (_registerAnchorDistances.GetDistance(index) == 1) ||
            (_externalAnchorDistances.GetDistance(index) == 1));
  }

  bool IsStaticAnchored(Index index) const {
    return index < _numAllocations &&
           _staticAnchorDistances.GetDistance(index) > 0;
  }

  bool IsStaticAnchorPoint(Index index) const {
    return index < _numAllocations &&
           _staticAnchorDistances.GetDistance(index) == 1;
  }

  const std::vector<Offset> *GetStaticAnchors(Index index) const {
    const std::vector<Offset> *result = 0;
    if (index < _numAllocations &&
        _staticAnchorDistances.GetDistance(index) == 1) {
      AnchorPointMapConstIterator it = _staticAnchorPoints.find(index);
      if (it != _staticAnchorPoints.end()) {
        result = &(it->second);
      }
    }
    return result;
  }

  bool IsStackAnchored(Index index) const {
    return index < _numAllocations &&
           _stackAnchorDistances.GetDistance(index) > 0;
  }

  bool IsStackAnchorPoint(Index index) const {
    return index < _numAllocations &&
           _stackAnchorDistances.GetDistance(index) == 1;
  }

  const std::vector<Offset> *GetStackAnchors(Index index) const {
    const std::vector<Offset> *result = 0;
    if (index < _numAllocations &&
        _stackAnchorDistances.GetDistance(index) == 1) {
      AnchorPointMapConstIterator it = _stackAnchorPoints.find(index);
      if (it != _stackAnchorPoints.end()) {
        result = &(it->second);
      }
    }
    return result;
  }

  bool IsRegisterAnchored(Index index) const {
    return index < _numAllocations &&
           _registerAnchorDistances.GetDistance(index) > 0;
  }

  bool IsRegisterAnchorPoint(Index index) const {
    return index < _numAllocations &&
           _registerAnchorDistances.GetDistance(index) == 1;
  }

  void GetRegisterAnchors(
      Index index,
      std::vector<std::pair<size_t, const char *> > anchors) const {
    anchors.clear();
    if (index < _numAllocations &&
        _registerAnchorDistances.GetDistance(index) == 1) {
      AnchorPointMapConstIterator it = _registerAnchorPoints.find(index);
      if (it != _registerAnchorPoints.end()) {
        size_t numRegisters = _threadMap.GetNumRegisters();
        const std::vector<Offset> &encodedAnchors = it->second;
        for (typename std::vector<Offset>::const_iterator it =
                 encodedAnchors.begin();
             it != encodedAnchors.end(); ++it) {
          Offset anchor = *it;
          size_t threadNum = anchor / numRegisters;
          const char *regName =
              _threadMap.GetRegisterName(anchor % numRegisters);
          anchors.push_back(std::make_pair(threadNum, regName));
        }
      }
    }
  }

  bool IsExternalAnchored(Index index) const {
    return index < _numAllocations &&
           _externalAnchorDistances.GetDistance(index) > 0;
  }

  bool IsExternalAnchorPoint(Index index) const {
    return index < _numAllocations &&
           _externalAnchorDistances.GetDistance(index) == 1;
  }

  bool IsThreadOnlyAnchored(Index index) const {
    return (index < _numAllocations) &&
           ((_registerAnchorDistances.GetDistance(index) > 0) ||
            (_stackAnchorDistances.GetDistance(index) > 0)) &&
           (_staticAnchorDistances.GetDistance(index) == 0) &&
           (_externalAnchorDistances.GetDistance(index) == 0);
  }

  bool IsThreadOnlyAnchorPoint(Index index) const {
    return (index < _numAllocations) &&
           ((_registerAnchorDistances.GetDistance(index) == 1) ||
            (_stackAnchorDistances.GetDistance(index) == 1)) &&
           (_staticAnchorDistances.GetDistance(index) == 0) &&
           (_externalAnchorDistances.GetDistance(index) == 0);
  }

  typedef bool (Graph::*LocalVisitor)(AnchorChainVisitor &visitor, Index index,
                                      Offset, Offset, const char *) const;
  bool VisitStaticAnchorPoint(AnchorChainVisitor &visitor, Index index,
                              Offset address, Offset size,
                              const char *image) const {
    if (index < _numAllocations &&
        _staticAnchorDistances.GetDistance(index) == 1) {
      AnchorPointMapConstIterator it = _staticAnchorPoints.find(index);
      if (it != _staticAnchorPoints.end()) {
        return visitor.VisitStaticAnchorChainHeader(it->second, address, size,
                                                    image);
      }
    }
    return false;
  }

  bool VisitStaticAnchorChains(Index index, AnchorChainVisitor &visitor) const {
    return VisitAnchorChains(index, visitor, _staticAnchorDistances,
                             &Graph::VisitStaticAnchorPoint);
  }

  bool VisitStackAnchorPoint(AnchorChainVisitor &visitor, Index index,
                             Offset address, Offset size,
                             const char *image) const {
    if (index < _numAllocations &&
        _stackAnchorDistances.GetDistance(index) == 1) {
      AnchorPointMapConstIterator it = _stackAnchorPoints.find(index);
      if (it != _stackAnchorPoints.end()) {
        return visitor.VisitStackAnchorChainHeader(it->second, address, size,
                                                   image);
      }
    }
    return false;
  }

  bool VisitStackAnchorChains(Index index, AnchorChainVisitor &visitor) const {
    return VisitAnchorChains(index, visitor, _stackAnchorDistances,
                             &Graph::VisitStackAnchorPoint);
  }

  bool VisitRegisterAnchorPoint(AnchorChainVisitor &visitor, Index index,
                                Offset address, Offset size,
                                const char *image) const {
    if (index < _numAllocations &&
        _registerAnchorDistances.GetDistance(index) == 1) {
      AnchorPointMapConstIterator it = _registerAnchorPoints.find(index);
      if (it != _registerAnchorPoints.end()) {
        std::vector<std::pair<size_t, const char *> > anchors;
        size_t numRegisters = _threadMap.GetNumRegisters();
        const std::vector<Offset> &encodedAnchors = it->second;
        for (typename std::vector<Offset>::const_iterator it =
                 encodedAnchors.begin();
             it != encodedAnchors.end(); ++it) {
          Offset anchor = *it;
          size_t threadNum = anchor / numRegisters;
          const char *regName =
              _threadMap.GetRegisterName(anchor % numRegisters);
          anchors.push_back(std::make_pair(threadNum, regName));
        }
        return visitor.VisitRegisterAnchorChainHeader(anchors, address, size,
                                                      image);
      }
    }
    return false;
  }

  bool VisitRegisterAnchorChains(Index index,
                                 AnchorChainVisitor &visitor) const {
    return VisitAnchorChains(index, visitor, _registerAnchorDistances,
                             &Graph::VisitRegisterAnchorPoint);
  }

  bool VisitExternalAnchorPoint(AnchorChainVisitor &visitor, Index index,
                                Offset address, Offset size,
                                const char *image) const {
    if (index < _numAllocations &&
        _externalAnchorDistances.GetDistance(index) == 1) {
      typename std::map<Index, const char *>::const_iterator it =
          _externalAnchorPoints.find(index);
      if (it != _externalAnchorPoints.end()) {
        return visitor.VisitExternalAnchorChainHeader(it->second, address, size,
                                                      image);
      }
    }
    return false;
  }

  bool VisitExternalAnchorChains(Index index,
                                 AnchorChainVisitor &visitor) const {
    return VisitAnchorChains(index, visitor, _externalAnchorDistances,
                             &Graph::VisitExternalAnchorPoint);
  }

  bool CallAnchorChainVisitor(Index index, AnchorChainVisitor &visitor,
                              LocalVisitor anchorPointVisitor) const {
    const Allocation *allocation = _directory.AllocationAt(index);
    if (allocation == 0 || !allocation->IsUsed()) {
      return false;
    }
    const char *image;
    Offset numBytesFound =
        _addressMap.FindMappedMemoryImage(allocation->Address(), &image);
    if (numBytesFound < sizeof(Offset)) {
      return false;
    }
    return (this->*anchorPointVisitor)(visitor, index, allocation->Address(),
                                       allocation->Size(), image);
  }

  bool VisitAnchorChains(Index index, AnchorChainVisitor &visitor,
                         const IndexedDistances<Index> &distances,
                         LocalVisitor anchorPointVisitor) const {
    if (index >= _numAllocations || _leaked[index]) {
      return false;
    }
    Index distance = distances.GetDistance(index);
    if (distance == 0) {
      // It is not anchored in this way.
      return false;
    }
    const Allocation *allocation = _directory.AllocationAt(index);
    if (allocation == 0 || !allocation->IsUsed()) {
      return false;
    }
    if (distance == 1 &&
        CallAnchorChainVisitor(index, visitor, anchorPointVisitor)) {
      // Under the given anchor type imposed by the distances argument, the
      // allocation to explain was directly anchored, so there is normally no
      // need to explain any indirect anchoring.
      return true;
    }
    // At this point the starting allocation is not directly anchored under
    // the given anchor type so we are interested in whether there is any
    // indirect anchoring.
    EdgeIndex edgeIndex = _firstIncoming[index];
    if (edgeIndex != _firstIncoming[index + 1]) {
      // There is at least one incoming edge.
      std::vector<bool> visited;
      visited.reserve(_numAllocations);
      visited.resize(_numAllocations, false);
      for (Index freeIndex = 0; freeIndex < _numAllocations; freeIndex++) {
        if (!_directory.AllocationAt(freeIndex)->IsUsed()) {
          visited[freeIndex] = true;
        }
      }

      // The edge target is already considered visited.
      visited[index] = true;
      std::vector<std::pair<Index, EdgeIndex> > edgesToVisit;
      edgesToVisit.push_back(std::make_pair(index, edgeIndex - 1));
      while (!edgesToVisit.empty()) {
        edgeIndex = ++(edgesToVisit.back().second);
        Index targetIndex = edgesToVisit.back().first;

        if (edgeIndex >= _firstIncoming[targetIndex + 1]) {
          // We have checked for any anchor paths that involve the
          // allocation corresponding to the target index as the target
          // of an edge.
          edgesToVisit.pop_back();
          continue;
        }

        Index sourceIndex = _incoming[edgeIndex];
        if (visited[sourceIndex]) {
          continue;
        } else {
          // The graph has both used and free nodes but here we are only
          // interested in paths involving used nodes.
          visited[sourceIndex] = true;
          const Allocation *allocation = _directory.AllocationAt(sourceIndex);
          if (allocation == 0 || !allocation->IsUsed()) {
            continue;
          }
        }

        Index sourceAnchorDistance = distances.GetDistance(sourceIndex);
        Index targetAnchorDistance = distances.GetDistance(targetIndex);
        if ((sourceAnchorDistance == 0) ||                    // leaked
            (sourceAnchorDistance > targetAnchorDistance) ||  // wrong way
            ((sourceAnchorDistance == targetAnchorDistance) &&
             (targetAnchorDistance != 0xFF))) {
          continue;
        }
        if (sourceAnchorDistance == 1) {
          // The source is an anchor point of the type associated with the
          // distances argument.
          if (CallAnchorChainVisitor(sourceIndex, visitor,
                                     anchorPointVisitor)) {
            return true;
          }

          for (typename std::vector<
                   std::pair<Index, EdgeIndex> >::const_reverse_iterator it =
                   edgesToVisit.rbegin();
               it != edgesToVisit.rend(); ++it) {
            Offset linkIndex = it->first;
            const Allocation *allocation = _directory.AllocationAt(linkIndex);
            if (allocation == 0 || !allocation->IsUsed()) {
              break;
            }
            const char *image;
            Offset numBytesFound = _addressMap.FindMappedMemoryImage(
                allocation->Address(), &image);
            if (numBytesFound < sizeof(Offset)) {
              break;
            }
            if (visitor.VisitChainLink(allocation->Address(),
                                       allocation->Size(), image)) {
              return true;
            }
          }
        }

        edgeIndex = _firstIncoming[sourceIndex];
        edgesToVisit.push_back(std::make_pair(sourceIndex, edgeIndex - 1));
      }
    }
    return false;
  }

  bool IsUnreferenced(Index index) const {
    bool isUnreferenced = false;
    if (index < _numAllocations && _leaked[index]) {
      const Index *pFirstIncomingIndex = &(_incoming[_firstIncoming[index]]);
      const Index *pPastIncomingIndex = &(_incoming[_firstIncoming[index + 1]]);
      isUnreferenced = true;
      for (const Index *pIncomingIndex = pFirstIncomingIndex;
           pIncomingIndex != pPastIncomingIndex; pIncomingIndex++) {
        if (_directory.AllocationAt((*pIncomingIndex))->IsUsed()) {
          isUnreferenced = false;
          break;
        }
      }
    }
    return isUnreferenced;
  }

 private:
  typedef std::vector<Offset> OffsetVector;
  typedef typename OffsetVector::iterator OffsetVectorIterator;
  typedef typename OffsetVector::const_iterator OffsetVectorConstIterator;
  typedef std::map<Index, OffsetVector> AnchorPointMap;
  typedef typename AnchorPointMap::iterator AnchorPointMapIterator;
  typedef typename AnchorPointMap::const_iterator AnchorPointMapConstIterator;
  const Directory<Offset> &_directory;
  const AddressMap &_addressMap;
  const ThreadMap<Offset> &_threadMap;
  const StackRegistry<Offset> &_stackRegistry;
  const ExternalAnchorPointChecker<Offset> *_externalAnchorPointChecker;
  const ObscuredReferenceChecker<Offset> *_obscuredReferenceChecker;
  Index _numAllocations;
  EdgeIndex _totalEdges;
  std::vector<Index> _outgoing;
  std::vector<Index> _incoming;
  std::vector<EdgeIndex> _firstOutgoing;
  std::vector<EdgeIndex> _firstIncoming;
  IndexedDistances<Index> _staticAnchorDistances;
  IndexedDistances<Index> _stackAnchorDistances;
  IndexedDistances<Index> _registerAnchorDistances;
  IndexedDistances<Index> _externalAnchorDistances;
  std::vector<bool> _leaked;
  AnchorPointMap _staticAnchorPoints;
  AnchorPointMap _stackAnchorPoints;
  AnchorPointMap _registerAnchorPoints;
  std::map<Index, const char *> _externalAnchorPoints;

  /*
   * Attempt to interpret the given target candidate as a reference to
   * an allocation, returning an index for that allocation if so.
   */
  Index EdgeTargetIndex(Offset targetCandidate) {
    Index targetIndex = _directory.AllocationIndexOf(targetCandidate);
    if (targetIndex == _numAllocations &&
        _obscuredReferenceChecker != nullptr) {
      targetIndex =
          _obscuredReferenceChecker->AllocationIndexOf(targetCandidate);
    }
    return targetIndex;
  }

  void FindEdges() {
    if (_numAllocations == 0) {
      return;
    }

    Offset maxAllocationSize = _directory.MaxAllocationSize();
    std::vector<Index> targets;
    targets.reserve(maxAllocationSize);

    _firstIncoming.reserve(_numAllocations + 1);
    _firstIncoming.resize(_numAllocations + 1, 0);
    _firstOutgoing.reserve(_numAllocations + 1);
    _firstOutgoing.resize(_numAllocations + 1, 0);

    /*
     * Count all the edges, but don't store them yet.  At the end of this
     * first pass, _firstOutgoing[i] will be set correctly to the index of
     * the first outgoing edge for allocation i in the array _outgoing,
     * but _firstIncoming[i] will have a temporary value of the number
     * of incoming edges for allocation i, rather than the correct index
     * into _incoming.
     */
    ContiguousImage<Offset> contiguousImage(_addressMap, _directory);
    Reader reader(_addressMap);
    for (Index i = 0; i < _numAllocations; i++) {
      contiguousImage.SetIndex(i);
      _firstOutgoing[i] = _totalEdges;
      // const Allocation *allocation = _directory.AllocationAt(i);

      /*
       * Note that we find all the edges, regardless of whether the source
       * or target is used or free.  Code that uses the graph is expected to
       * check the source and/or the target when one particular usage status
       * is required.
       */
      targets.clear();
      Index prevTarget = _numAllocations;
      const Offset *offsetLimit = contiguousImage.OffsetLimit();
      for (const Offset *check = contiguousImage.FirstOffset();
           check < offsetLimit; check++) {
        Index target = EdgeTargetIndex(*check);
        if (target != _numAllocations && target != i && target != prevTarget) {
          targets.push_back(target);
          prevTarget = target;
        }
      }
      if (!targets.empty()) {
        if (targets.size() > 1) {
          std::sort(targets.begin(), targets.end());
        }
        Index prevTarget = _numAllocations;
        for (Offset target : targets) {
          if (target != prevTarget) {
            _firstIncoming[target]++;
            _totalEdges++;
            prevTarget = target;
          }
        }
      }
    }
    _firstOutgoing[_numAllocations] = _totalEdges;

    /*
     * Convert values in _firstIncoming from incoming edge counts to offsets
     * just after incoming edges.
     */

    for (Index i = 0; i < _numAllocations; i++) {
      _firstIncoming[i + 1] = _firstIncoming[i] + _firstIncoming[i + 1];
    }
    _outgoing.reserve(_totalEdges);
    _outgoing.resize(_totalEdges, 0);
    _incoming.reserve(_totalEdges);
    _incoming.resize(_totalEdges, 0);

    /*
     * Fill in the outgoing and incoming edges and convert values in
     * _firstIncoming to indicate the index of the first incoming edge
     * for the corresponding node in _incoming.  Go backwards in the
     * sources so that the incoming edges in _incoming have subranges
     * in increasing order of target, where the values in each subrange
     * are the sources in increasing order.
     */

    for (Index i = _numAllocations; i > 0;) {
      contiguousImage.SetIndex(--i);
      /*
       * Note that we find all the edges, regardless of whether the source
       * or target is used or free.  Code that uses the graph is expected to
       * check the source and/or the target when one particular usage status
       * is required.
       */
      targets.clear();
      Index prevTarget = _numAllocations;
      const Offset *offsetLimit = contiguousImage.OffsetLimit();
      for (const Offset *check = contiguousImage.FirstOffset();
           check < offsetLimit; check++) {
        Index target = EdgeTargetIndex(*check);
        if (target != _numAllocations && target != i && target != prevTarget) {
          targets.push_back(target);
          prevTarget = target;
        }
      }
      if (!targets.empty()) {
        if (targets.size() > 1) {
          std::sort(targets.begin(), targets.end());
        }
        Index prevTarget = _numAllocations;
        EdgeIndex nextOutgoing = _firstOutgoing[i];
        for (Offset target : targets) {
          if (target != prevTarget) {
            _incoming[--_firstIncoming[target]] = i;
            _outgoing[nextOutgoing++] = target;
            prevTarget = target;
          }
        }
      }
    }
  }

  void MarkAnchoredChunks(AnchorPointMap &anchorPoints,
                          IndexedDistances<Index> &anchorDistance) {
    std::vector<bool> visited;
    visited.reserve(_numAllocations);
    visited.resize(_numAllocations, false);
    for (Index index = 0; index < _numAllocations; index++) {
      if (!_directory.AllocationAt(index)->IsUsed()) {
        visited[index] = true;
      }
    }
    std::deque<Index> toVisit;
    AnchorPointMapConstIterator itEnd = anchorPoints.end();
    for (AnchorPointMapConstIterator it = anchorPoints.begin(); it != itEnd;
         ++it) {
      Index index = it->first;
      visited[index] = true;
      _leaked[index] = false;
      anchorDistance.SetDistance(index, 1);
      toVisit.push_back(index);
    }
    while (!toVisit.empty()) {
      Index sourceIndex = toVisit.front();
      Index newDistance = anchorDistance.GetDistance(sourceIndex) + 1;
      toVisit.pop_front();
      EdgeIndex edgeLimit = _firstOutgoing[sourceIndex + 1];
      for (EdgeIndex edgeIndex = _firstOutgoing[sourceIndex];
           edgeIndex < edgeLimit; edgeIndex++) {
        Index targetIndex = _outgoing[edgeIndex];
        if (!visited[targetIndex]) {
          visited[targetIndex] = true;
          _leaked[targetIndex] = false;
          anchorDistance.SetDistance(targetIndex, newDistance);
          toVisit.push_back(targetIndex);
        }
      }
    }
  }

  void MarkAnchoredChunks(std::map<Index, const char *> &anchorPoints,
                          IndexedDistances<Index> &anchorDistance) {
    std::vector<bool> visited;
    visited.reserve(_numAllocations);
    visited.resize(_numAllocations, false);
    for (Index index = 0; index < _numAllocations; index++) {
      if (!_directory.AllocationAt(index)->IsUsed()) {
        visited[index] = true;
      }
    }
    std::deque<Index> toVisit;
    typename std::map<Index, const char *>::const_iterator itEnd =
        anchorPoints.end();
    for (typename std::map<Index, const char *>::const_iterator it =
             anchorPoints.begin();
         it != itEnd; ++it) {
      Index index = it->first;
      visited[index] = true;
      _leaked[index] = false;
      anchorDistance.SetDistance(index, 1);
      toVisit.push_back(index);
    }
    while (!toVisit.empty()) {
      Index sourceIndex = toVisit.front();
      Index newDistance = anchorDistance.GetDistance(sourceIndex) + 1;
      toVisit.pop_front();
      EdgeIndex edgeLimit = _firstOutgoing[sourceIndex + 1];
      for (EdgeIndex edgeIndex = _firstOutgoing[sourceIndex];
           edgeIndex < edgeLimit; edgeIndex++) {
        Index targetIndex = _outgoing[edgeIndex];
        if (!visited[targetIndex]) {
          visited[targetIndex] = true;
          _leaked[targetIndex] = false;
          anchorDistance.SetDistance(targetIndex, newDistance);
          toVisit.push_back(targetIndex);
        }
      }
    }
  }

  void FindAnchorPoints(Offset rangeBase, Offset rangeEnd,
                        AnchorPointMap &anchorPoints) {
    Reader reader(_addressMap);
    for (Offset anchor = rangeBase; anchor < rangeEnd;
         anchor += sizeof(Offset)) {
      try {
        Offset candidateTarget = reader.ReadOffset(anchor);
        Index targetIndex = EdgeTargetIndex(candidateTarget);
        const Allocation *target = _directory.AllocationAt(targetIndex);
        if ((target != 0) && target->IsUsed()) {
          AnchorPointMapIterator it = anchorPoints.find(targetIndex);
          if (it == anchorPoints.end()) {
            it = anchorPoints
                     .insert(std::make_pair(targetIndex, std::vector<Offset>()))
                     .first;
          }
          it->second.push_back(anchor);
        }
      } catch (NotMapped &) {
      }
    }
  }

  void FindStaticAnchorPoints(
      const std::map<Offset, Offset> &staticAnchorLimits) {
    typename std::map<Offset, Offset>::const_iterator itEnd =
        staticAnchorLimits.end();
    for (typename std::map<Offset, Offset>::const_iterator it =
             staticAnchorLimits.begin();
         it != itEnd; ++it) {
      FindAnchorPoints(it->first, it->second, _staticAnchorPoints);
    }
  }

  void FindStackAnchorPoints() {
    // TODO: Fix this for the case of cached stacks, which should mostly be
    // not considered to have static anchors, with the exception of left
    // over thread locals.
    // TODO: Fix this for the case of a pthread stack where the associated
    //       thread has switched to a different kind of stack, such as
    //       a goroutine.
    // TODO: When stack base (as opposed to region base) becomes available,
    //       use it, but this presupposes a new category of thread-local
    //       anchor.
    _stackRegistry.VisitStacks([&](Offset regionBase, Offset regionLimit,
                                   const char *, Offset stackTop, Offset,
                                   size_t) {
      FindAnchorPoints((stackTop != StackRegistry<Offset>::STACK_TOP_UNKNOWN)
                           ? stackTop
                           : regionBase,
                       regionLimit, _stackAnchorPoints);
      return true;
    });
  }

  void FindRegisterAnchorPoints() {
    size_t numRegisters = _threadMap.GetNumRegisters();

    typename ThreadMap<Offset>::const_iterator itEnd = _threadMap.end();
    for (typename ThreadMap<Offset>::const_iterator it = _threadMap.begin();
         it != itEnd; ++it) {
      Offset *registers = it->_registers;
      for (size_t i = 0; i < numRegisters; ++i) {
        Offset candidateTarget = registers[i];
        if (candidateTarget != 0 &&
            (candidateTarget & (sizeof(Offset) - 1)) == 0) {
          Index targetIndex = _directory.AllocationIndexOf(candidateTarget);
          const Allocation *target = _directory.AllocationAt(targetIndex);
          if ((target != 0) && target->IsUsed()) {
            AnchorPointMapIterator itAnchor =
                _registerAnchorPoints.find(targetIndex);
            if (itAnchor == _registerAnchorPoints.end()) {
              itAnchor = _registerAnchorPoints
                             .insert(std::make_pair(targetIndex,
                                                    std::vector<Offset>()))
                             .first;
            }
            itAnchor->second.push_back(it->_threadNum * numRegisters + i);
          }
        }
      }
    }
  }

  void FindExternalAnchorPoints() {
    if (_externalAnchorPointChecker != 0) {
      ContiguousImage<Offset> contiguousImage(_addressMap, _directory);
      for (Index i = 0; i < _numAllocations; i++) {
        if (_directory.AllocationAt(i)->IsUsed()) {
          contiguousImage.SetIndex(i);
          const char *externalAnchorReason =
              _externalAnchorPointChecker->GetExternalAnchorReason(
                  i, contiguousImage);
          if (externalAnchorReason != (const char *)0) {
            _externalAnchorPoints[i] = externalAnchorReason;
          }
        }
      }
    }
  }

  void MarkLeakedChunks() {
    _leaked.reserve(_numAllocations);
    _leaked.resize(_numAllocations, true);
    for (Index i = 0; i < _numAllocations; i++) {
      if (!_directory.AllocationAt(i)->IsUsed()) {
        _leaked[i] = false;
      }
    }
    MarkAnchoredChunks(_staticAnchorPoints, _staticAnchorDistances);
    MarkAnchoredChunks(_stackAnchorPoints, _stackAnchorDistances);
    MarkAnchoredChunks(_registerAnchorPoints, _registerAnchorDistances);
    MarkAnchoredChunks(_externalAnchorPoints, _externalAnchorDistances);
  }
};
}  // namespace Allocations
}  // namespace chap
