// Copyright (c) 2019-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/Graph.h"
#include "Allocations/TagHolder.h"
#include "Allocations/Tagger.h"
#include "VirtualAddressMap.h"

namespace chap {
template <typename Offset>
class ListAllocationsTagger : public Allocations::Tagger<Offset> {
 public:
  typedef typename Allocations::Graph<Offset> Graph;
  typedef typename Allocations::Directory<Offset> Directory;
  typedef typename Allocations::Tagger<Offset> Tagger;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename Tagger::Phase Phase;
  typedef typename Directory::AllocationIndex AllocationIndex;
  typedef typename Directory::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename TagHolder::TagIndex TagIndex;
  ListAllocationsTagger(Graph& graph, TagHolder& tagHolder)
      : _graph(graph),
        _tagHolder(tagHolder),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _addressMap(graph.GetAddressMap()),
        _nodeReader(_addressMap),
        _nodeTagIndex(_tagHolder.RegisterTag("%ListNode")),
        _unknownHeadNodeTagIndex(_tagHolder.RegisterTag("%ListNode")) {}

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool isUnsigned) {
    if (!isUnsigned) {
      return true;
    }
    return TagFromListNode(contiguousImage, index, phase, allocation);
  }

  TagIndex GetNodeTagIndex() const { return _nodeTagIndex; }
  TagIndex GetUnknownHeadNodeTagIndex() const {
    return _unknownHeadNodeTagIndex;
  }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  const Directory& _directory;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  Reader _nodeReader;
  TagIndex _nodeTagIndex;
  TagIndex _unknownHeadNodeTagIndex;
  Offset _address;
  Offset _size;
  Offset _next;
  Offset _prev;

  bool TagFromListNode(const ContiguousImage& contiguousImage,
                       AllocationIndex index, Phase phase,
                       const Allocation& allocation) {
    const Offset* firstOffset = contiguousImage.FirstOffset();
    const Offset* offsetLimit = contiguousImage.OffsetLimit();
    _address = allocation.Address();
    _size = allocation.Size();
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        if (_tagHolder.IsStronglyTagged(index)) {
          /*
           * This was already strongly tagged, generally as a result of following
           * outgoing references from an allocation already being tagged.
           * From this we conclude that the given allocation is not a list node
           * or that it was already tagged as such.
           */
          return true;  // No more need to look at this allocation for this pass
        }
        if (offsetLimit - firstOffset < 3) {
          return true;
        }
        _next = firstOffset[0];
        if (_next == 0 || (_next & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        _prev = firstOffset[1];
        if (_prev == 0 || (_prev & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        if (_nodeReader.ReadOffset(_next + sizeof(Offset), 0) != _address) {
          return true;
        }
        if (_nodeReader.ReadOffset(_prev, 0) != _address) {
          return true;
        }
        {
          AllocationIndex nextIndex =
              _graph.TargetAllocationIndex(index, _next);
          if (nextIndex != _numAllocations) {
            const Allocation* nextAllocation =
                _directory.AllocationAt(nextIndex);
            if (nextAllocation->Address() == _next &&
                _tagHolder.GetTagIndex(nextIndex) == _nodeTagIndex) {
              /*
               * This node happens to have a list header at the start, but the
               * list was already found when one of the entries on the list was
               * visited.
               */
              return true;
            }
          }
        }
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckList(index);
        return true;
        break;
      case Tagger::WEAK_CHECK:
        // May be expensive, weak results OK
        // An example here might be if one of the nodes in the chain is no
        // longer allocated.
        break;
    }
    return false;
  }
  void CheckList(AllocationIndex index) {
    Offset expectPrev = _address;
    Offset node = _next;
    Offset listHead = 0;
    AllocationIndex listSize = 0;
    AllocationIndex sourceIndex = index;
    for (; node != _address && listSize < _numAllocations; listSize++) {
      if (_nodeReader.ReadOffset(node + sizeof(Offset), 0xbad) != expectPrev) {
        return;
      }
      AllocationIndex nodeIndex =
          _graph.TargetAllocationIndex(sourceIndex, node);
      if (nodeIndex == _numAllocations) {
        if (listHead != 0) {
          return;
        }
        listHead = node;
      } else {
        const Allocation* allocation = _directory.AllocationAt(nodeIndex);
        if (allocation->Address() != node) {
          if (listHead != 0) {
            return;
          }
          listHead = node;
        }
      }
      expectPrev = node;
      sourceIndex = nodeIndex;
      node = _nodeReader.ReadOffset(node, 0xbad);
      if (node == 0 || (node & (sizeof(Offset) - 1)) != 0) {
        return;
      }
    }
    if (node != _address) {
      return;
    }
    if (listHead == 0) {
      /*
       * We made it around the ring but couldn't figure out the header.  Try
       * to figure out the header by finding a node that is different from
       * the others, either in content or in how it is referenced.
       */
      listHead = FindListHeadByPointersToStart(index);
      if (listHead == 0) {
        listHead = FindListHeadBySizeOutlier(index);
        if (listHead == 0) {
          /*
           * It would be better to add more ways to find the list head here,
           * but for now it is best to just mark all the nodes as belonging to
           * a list with an unknown head, particularly because failing to do so
           * will result in our doing the same failed calculation on all items
           * of the list.
           */
          TagAllAsHavingUnknownHead(index);
          return;
        }
      }
    }
    TagNodesWithKnownHead(index, listHead);
  }

  void TagAllExceptStartNode(AllocationIndex index, TagIndex tagIndex) {
    AllocationIndex sourceIndex = index;
    for (Offset node = _next; node != _address;
         node = _nodeReader.ReadOffset(node, 0xbad)) {
      AllocationIndex nodeIndex =
          _graph.TargetAllocationIndex(sourceIndex, node);
      _tagHolder.TagAllocation(nodeIndex, tagIndex);
      sourceIndex = nodeIndex;
    }
  }

  void TagAllAsHavingUnknownHead(AllocationIndex index) {
    _tagHolder.TagAllocation(index, _unknownHeadNodeTagIndex);
    TagAllExceptStartNode(index, _unknownHeadNodeTagIndex);
  }

  void TagNodesWithKnownHead(AllocationIndex index, Offset listHead) {
    if (listHead == _address) {
      TagAllExceptStartNode(index, _nodeTagIndex);
    } else {
      _tagHolder.TagAllocation(index, _nodeTagIndex);
      Offset node = _next;
      Offset sourceIndex = index;
      while (node != listHead) {
        AllocationIndex nodeIndex =
            _graph.TargetAllocationIndex(sourceIndex, node);
        _tagHolder.TagAllocation(nodeIndex, _nodeTagIndex);
        sourceIndex = nodeIndex;
        node = _nodeReader.ReadOffset(node, 0xbad);
      }
      node = _prev;
      sourceIndex = index;
      while (node != listHead) {
        AllocationIndex nodeIndex =
            _graph.TargetAllocationIndex(sourceIndex, node);
        _tagHolder.TagAllocation(nodeIndex, _nodeTagIndex);
        sourceIndex = nodeIndex;
        node = _nodeReader.ReadOffset(node + sizeof(Offset), 0xbad);
      }
    }
  }

  Offset FindListHeadByPointersToStart(AllocationIndex index) {
    Reader refReader(_addressMap);
    Offset listHead = 0;
    Offset node = _address;
    AllocationIndex nodeIndex = index;
    Offset prev = _prev;
    do {
      Offset next = _nodeReader.ReadOffset(node, 0xbad);
      if (HasAnchorToStart(_graph.GetStaticAnchors(nodeIndex), node,
                           refReader) ||
          HasAnchorToStart(_graph.GetStackAnchors(nodeIndex), node,
                           refReader) ||
          HasExtraPointerToStartFromAllocation(nodeIndex, node, next, prev,
                                               refReader)) {
        if (listHead != 0) {
          // We can't disambiguate this way because there are at least 2
          // candidates.
          return 0;
        }
        listHead = node;
      }
      prev = node;
      nodeIndex = _graph.TargetAllocationIndex(nodeIndex, next);
      node = next;
    } while (node != _address);
    return listHead;
  }

  Offset FindListHeadBySizeOutlier(AllocationIndex index) {
    if (_next == _prev) {
      /*
       * We can't vote by size if there is just one element on the list.
       */
      return 0;
    }
    Offset totalSize = 0;
    Offset totalCount = 0;
    Offset listHead = 0;

    Offset node = _address;
    AllocationIndex nodeIndex = index;
    do {
      const Allocation* allocation = _directory.AllocationAt(nodeIndex);
      totalSize += allocation->Size();
      totalCount++;
      node = _nodeReader.ReadOffset(node, 0xbad);
      nodeIndex = _graph.TargetAllocationIndex(nodeIndex, node);
    } while (node != _address);

    Offset averageSize = totalSize / totalCount;
    Offset minSize = averageSize - 2 * sizeof(Offset);
    Offset maxSize = averageSize + 2 * sizeof(Offset);

    node = _address;
    nodeIndex = index;
    do {
      const Allocation* allocation = _directory.AllocationAt(nodeIndex);
      Offset nodeSize = allocation->Size();
      if (minSize > nodeSize || nodeSize > maxSize) {
        if (listHead != 0) {
          // We can't disambiguate this way because there are at least 2
          // candidates.
          return 0;
        }
        listHead = node;
      }
      node = _nodeReader.ReadOffset(node, 0xbad);
      nodeIndex = _graph.TargetAllocationIndex(nodeIndex, node);
    } while (node != _address);
    return listHead;
  }

  bool HasAnchorToStart(const std::vector<Offset>* anchors, Offset node,
                        Reader& refReader) {
    if (anchors != nullptr) {
      for (auto anchor : (*anchors)) {
        if (refReader.ReadOffset(anchor, 0xbad) == node) {
          return true;
        }
      }
    }
    return false;
  }

  bool HasExtraPointerToStartFromAllocation(AllocationIndex index, Offset node,
                                            Offset next, Offset prev,
                                            Reader& refReader) {
    const AllocationIndex* pFirstIncoming;
    const AllocationIndex* pPastIncoming;
    _graph.GetIncoming(index, &pFirstIncoming, &pPastIncoming);
    for (const AllocationIndex* pNextIncoming = pFirstIncoming;
         pNextIncoming != pPastIncoming; ++pNextIncoming) {
      const Allocation* incomingAllocation =
          _directory.AllocationAt(*pNextIncoming);
      Offset incomingAddress = incomingAllocation->Address();
      if (incomingAddress == next || incomingAddress == prev) {
        continue;
      }
      Offset incomingSize = incomingAllocation->Size();
      if (incomingSize < sizeof(Offset)) {
        continue;
      }
      Offset checkAt = incomingAllocation->Address();
      Offset checkLimit = checkAt + (incomingSize & ~(sizeof(Offset) - 1));
      for (; checkAt < checkLimit; checkAt++) {
        if (refReader.ReadOffset(checkAt, 0xbad) == node) {
          return true;
        }
      }
    }
    return false;
  }
};
}  // namespace chap
