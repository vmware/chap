// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
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
  typedef typename Allocations::Finder<Offset> Finder;
  typedef typename Allocations::Tagger<Offset> Tagger;
  typedef typename Tagger::Pass Pass;
  typedef typename Tagger::Phase Phase;
  typedef typename Finder::AllocationIndex AllocationIndex;
  typedef typename Finder::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename TagHolder::TagIndex TagIndex;
  ListAllocationsTagger(Graph& graph, TagHolder& tagHolder)
      : _graph(graph),
        _tagHolder(tagHolder),
        _finder(graph.GetAllocationFinder()),
        _numAllocations(_finder.NumAllocations()),
        _addressMap(_finder.GetAddressMap()),
        _nodeTagIndex(_tagHolder.RegisterTag("list node")),
        _unknownHeadNodeTagIndex(
            _tagHolder.RegisterTag("list node-unknown-head")) {}

  bool TagFromAllocation(Reader& reader, Pass pass, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool isUnsigned) {
    if (!isUnsigned) {
      return true;
    }
    switch (pass) {
      case Tagger::FIRST_PASS_THROUGH_ALLOCATIONS:
        /*
         * We can safely tag during the first pass because a list node is
         * somewhat exactly recognized, particularly in the context of the
         * entire list.
         */
        return TagFromListNode(reader, index, phase, allocation);
        break;
      case Tagger::LAST_PASS_THROUGH_ALLOCATIONS:
        /*
         * No tagging occurs in the second pass through the allocations.
         */
        break;
    }
    return true;
  }

  TagIndex GetNodeTagIndex() const { return _nodeTagIndex; }
  TagIndex GetUnknownHeadNodeTagIndex() const {
    return _unknownHeadNodeTagIndex;
  }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  const Finder& _finder;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  TagIndex _nodeTagIndex;
  TagIndex _unknownHeadNodeTagIndex;
  Offset _address;
  Offset _size;
  Offset _next;
  Offset _prev;

  bool TagFromListNode(Reader& reader, AllocationIndex index, Phase phase,
                       const Allocation& allocation) {
    Reader nodeReader(_addressMap);

    _address = allocation.Address();
    _size = allocation.Size();
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        if (_tagHolder.GetTagIndex(index) != 0) {
          /*
           * This was already tagged, generally as a result of following
           * outgoing references from an allocation already being tagged.
           * From this we conclude that the given allocation is not a list node
           * or that it was already tagged as such.
           */
          return true;  // No more need to look at this allocation for this pass
        }
        if (_size < 3 * sizeof(Offset)) {
          return true;
        }
        _next = reader.ReadOffset(_address, 0);
        if (_next == 0 || (_next & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        _prev = reader.ReadOffset(_address + sizeof(Offset), 0);
        if (_prev == 0 || (_prev & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        if (nodeReader.ReadOffset(_next + sizeof(Offset), 0) != _address) {
          return true;
        }
        if (nodeReader.ReadOffset(_prev, 0) != _address) {
          return true;
        }
        {
          AllocationIndex nextIndex = _finder.AllocationIndexOf(_next);
          if (nextIndex != _numAllocations) {
            const Allocation* nextAllocation = _finder.AllocationAt(nextIndex);
            if (nextAllocation->Address() == _next &&
                _tagHolder.GetTagIndex(nextIndex) == _nodeTagIndex) {
              /*
               * This node happens to have a list header at the start, but the
               * list was already found when one of the entries on the list was
               * visited.
               */
            }
          }
        }
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckList(index, nodeReader);
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
  void CheckList(AllocationIndex index, Reader& nodeReader) {
    Offset expectPrev = _address;
    Offset node = _next;
    Offset listHead = 0;
    AllocationIndex listSize = 0;
    for (; node != _address && listSize < _numAllocations; listSize++) {
      if (nodeReader.ReadOffset(node + sizeof(Offset), 0xbad) != expectPrev) {
        return;
      }
      AllocationIndex nodeIndex = _finder.AllocationIndexOf(node);
      if (nodeIndex == _numAllocations) {
        if (listHead != 0) {
          return;
        }
        listHead = node;
      } else {
        const Allocation* allocation = _finder.AllocationAt(nodeIndex);
        if (allocation->Address() != node) {
          if (listHead != 0) {
            return;
          }
          listHead = node;
        }
      }
      expectPrev = node;
      node = nodeReader.ReadOffset(node, 0xbad);
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
      listHead = FindListHeadByPointersToStart(nodeReader);
      if (listHead == 0) {
        listHead = FindListHeadBySizeOutlier(nodeReader);
        if (listHead == 0) {
          /*
           * It would be better to add more ways to find the list head here,
           * but for now it is best to just mark all the nodes as belonging to
           * a list with an unknown head, particularly because failing to do so
           * will result in our doing the same failed calculation on all items
           * of the list.
           */
          TagAllAsHavingUnknownHead(index, nodeReader);
          return;
        }
      }
    }
    TagNodesWithKnownHead(listHead, nodeReader);
  }

  void TagAllAsHavingUnknownHead(AllocationIndex index, Reader& nodeReader) {
    _tagHolder.TagAllocation(index, _unknownHeadNodeTagIndex);
    for (Offset node = _next; node != _address;
         node = nodeReader.ReadOffset(node, 0xbad)) {
      _tagHolder.TagAllocation(_finder.AllocationIndexOf(node),
                               _unknownHeadNodeTagIndex);
    }
  }

  void TagNodesWithKnownHead(Offset listHead, Reader& nodeReader) {
    for (Offset node = nodeReader.ReadOffset(listHead, 0xbad); node != listHead;
         node = nodeReader.ReadOffset(node, 0xbad)) {
      _tagHolder.TagAllocation(_finder.AllocationIndexOf(node), _nodeTagIndex);
    }
  }

  Offset FindListHeadByPointersToStart(Reader& nodeReader) {
    Reader refReader(_addressMap);
    Offset listHead = 0;
    Offset node = _address;
    Offset prev = _prev;
    do {
      AllocationIndex index = _finder.AllocationIndexOf(node);
      Offset next = nodeReader.ReadOffset(node, 0xbad);
      if (HasAnchorToStart(_graph.GetStaticAnchors(index), node, refReader) ||
          HasAnchorToStart(_graph.GetStackAnchors(index), node, refReader) ||
          HasExtraPointerToStartFromAllocation(index, node, next, prev,
                                               refReader)) {
        if (listHead != 0) {
          // We can't disambiguate this way because there are at least 2
          // candidates.
          return 0;
        }
        listHead = node;
      }
      prev = node;
      node = next;
    } while (node != _address);
    return listHead;
  }

  Offset FindListHeadBySizeOutlier(Reader& nodeReader) {
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
    do {
      AllocationIndex index = _finder.AllocationIndexOf(node);
      const Allocation* allocation = _finder.AllocationAt(index);
      totalSize += allocation->Size();
      totalCount++;
      node = nodeReader.ReadOffset(node, 0xbad);
    } while (node != _address);

    Offset averageSize = totalSize / totalCount;
    Offset minSize = averageSize - 2 * sizeof(Offset);
    Offset maxSize = averageSize + 2 * sizeof(Offset);

    node = _address;
    do {
      AllocationIndex index = _finder.AllocationIndexOf(node);
      const Allocation* allocation = _finder.AllocationAt(index);
      Offset nodeSize = allocation->Size();
      if (minSize > nodeSize || nodeSize > maxSize) {
        if (listHead != 0) {
          // We can't disambiguate this way because there are at least 2
          // candidates.
          return 0;
        }
        listHead = node;
      }
      node = nodeReader.ReadOffset(node, 0xbad);
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
          _finder.AllocationAt(*pNextIncoming);
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
