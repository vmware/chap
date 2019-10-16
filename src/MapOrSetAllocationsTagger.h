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
class MapOrSetAllocationsTagger : public Allocations::Tagger<Offset> {
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
  MapOrSetAllocationsTagger(Graph& graph, TagHolder& tagHolder)
      : _graph(graph),
        _tagHolder(tagHolder),
        _finder(graph.GetAllocationFinder()),
        _numAllocations(_finder.NumAllocations()),
        _addressMap(_finder.GetAddressMap()),
        _nodeTagIndex(_tagHolder.RegisterTag("set or map node")) {}

  bool TagFromAllocation(Reader& reader, Pass pass, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    /*
     * Note that we don't bother to check whether the allocation is unsigned
     * because only the least significant byte is set out of the first Offset,
     * meaning that a node in a map or set may give the illusion of being
     * signed.
     */
    switch (pass) {
      case Tagger::FIRST_PASS_THROUGH_ALLOCATIONS:
        /*
         * We can safely tag during the first pass because the root node
         * of a set or map is easily recognized.
         */
        return TagFromRootNode(reader, index, phase, allocation);
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

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  const Finder& _finder;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  TagIndex _nodeTagIndex;
  Offset _parent;
  Offset _leftChild;
  Offset _rightChild;
  Offset _firstNode;
  Offset _lastNode;
  Offset _mapOrSetSize;

  bool TagFromRootNode(Reader& reader, AllocationIndex index, Phase phase,
                       const Allocation& allocation) {
    if (_tagHolder.GetTagIndex(index) != 0) {
      /*
       * This was already tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation is not the root
       * node for a map or set.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }
    Reader otherReader(_addressMap);

    Offset size = allocation.Size();
    Offset address = allocation.Address();
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        if (size < 5 * sizeof(Offset) ||
            (reader.ReadOffset(address, 0xbad) & 0xfe) != 0) {
          return true;
        }
        _parent = reader.ReadOffset(address + sizeof(Offset), 0xbad);
        if (_parent == 0 || (_parent & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        _leftChild = reader.ReadOffset(address + 2 * sizeof(Offset), 0xbad);
        if ((_leftChild & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        _rightChild = reader.ReadOffset(address + 3 * sizeof(Offset), 0xbad);
        if ((_rightChild & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        if ((reader.ReadOffset(_parent, 0xbad) & 0xfe) != 0) {
          return true;
        }
        if (address !=
            otherReader.ReadOffset(_parent + sizeof(Offset), 0xbad)) {
          return true;
        }
        _firstNode =
            otherReader.ReadOffset(_parent + 2 * sizeof(Offset), 0xbad);
        if (_firstNode == 0 || (_firstNode & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        _lastNode = otherReader.ReadOffset(_parent + 3 * sizeof(Offset), 0xbad);
        if (_lastNode == 0 || (_lastNode & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        if ((_leftChild == 0) != (_firstNode == address)) {
          return true;
        }
        if ((_rightChild == 0) != (_lastNode == address)) {
          return true;
        }
        _mapOrSetSize =
            otherReader.ReadOffset(_parent + 4 * sizeof(Offset), 0xbad);
        if (_mapOrSetSize == 0) {
          return true;
        }
        if (otherReader.ReadOffset(_firstNode + 2 * sizeof(Offset), 0xbad) !=
            0) {
          return true;
        }
        if (otherReader.ReadOffset(_lastNode + 3 * sizeof(Offset), 0xbad) !=
            0) {
          return true;
        }
        if (_mapOrSetSize == 1) {
          if (_leftChild == 0 && _rightChild == 0) {
            // This is a trivial map or set of size 1.
            _tagHolder.TagAllocation(index, _nodeTagIndex);
          }
          return true;
        }
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        if (_mapOrSetSize <= 7) {
          CheckAllMapOrSetNodes(otherReader);
          return true;
        }
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckAllMapOrSetNodes(otherReader);
        break;
      case Tagger::WEAK_CHECK:
        // May be expensive, weak results OK
        // An example here might be if one of the nodes in the chain is no
        // longer allocated.
        break;
    }
    return false;
  }
  void CheckAllMapOrSetNodes(Reader& otherReader) {
    Offset numVisited = 0;
    Offset node = _firstNode;
    while (numVisited < _mapOrSetSize && node != _parent) {
      if ((otherReader.ReadOffset(node, 0xbad) & 0xfe) != 0) {
        return;
      }
      AllocationIndex index = _finder.AllocationIndexOf(node);
      if (index == _numAllocations) {
        return;
      }
      const Allocation* allocation = _finder.AllocationAt(index);
      if (allocation == nullptr) {
        return;
      }
      if (allocation->Size() < 5 * sizeof(Offset)) {
        return;
      }
      if (allocation->Address() != node) {
        return;
      }

      ++numVisited;

      Offset rightChild =
          otherReader.ReadOffset(node + 3 * sizeof(Offset), 0xbad);
      if (rightChild != 0) {
        if ((rightChild & (sizeof(Offset) - 1)) != 0) {
          return;
        }
        node = rightChild;
        Offset leftChild =
            otherReader.ReadOffset(node + 2 * sizeof(Offset), 0xbad);
        while (leftChild != 0) {
          node = leftChild;
          leftChild = otherReader.ReadOffset(node + 2 * sizeof(Offset), 0xbad);
        }
      } else {
        Offset parent = otherReader.ReadOffset(node + sizeof(Offset), 0xbad);
        while (parent != _parent &&
               otherReader.ReadOffset(parent + 3 * sizeof(Offset), 0xbad) ==
                   node) {
          node = parent;
          parent = otherReader.ReadOffset(node + sizeof(Offset), 0xbad);
        }
        if (parent != _parent &&
            otherReader.ReadOffset(parent + 2 * sizeof(Offset), 0xbad) !=
                node) {
          return;
        }
        node = parent;
      }
    }
    if (numVisited != _mapOrSetSize || node != _parent) {
      return;
    }

    node = _firstNode;
    while (node != _parent) {
      _tagHolder.TagAllocation(_finder.AllocationIndexOf(node), _nodeTagIndex);

      Offset rightChild =
          otherReader.ReadOffset(node + 3 * sizeof(Offset), 0xbad);
      if (rightChild != 0) {
        node = rightChild;
        Offset leftChild =
            otherReader.ReadOffset(node + 2 * sizeof(Offset), 0xbad);
        while (leftChild != 0) {
          node = leftChild;
          leftChild = otherReader.ReadOffset(node + 2 * sizeof(Offset), 0xbad);
        }
      } else {
        Offset parent = otherReader.ReadOffset(node + sizeof(Offset), 0xbad);
        while (parent != _parent &&
               otherReader.ReadOffset(parent + 3 * sizeof(Offset), 0xbad) ==
                   node) {
          node = parent;
          parent = otherReader.ReadOffset(node + sizeof(Offset), 0xbad);
        }
        node = parent;
      }
    }
  }
};
}  // namespace chap
