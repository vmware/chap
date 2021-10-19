// Copyright (c) 2019-2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/EdgePredicate.h"
#include "Allocations/Graph.h"
#include "Allocations/TagHolder.h"
#include "Allocations/Tagger.h"
#include "VirtualAddressMap.h"

namespace chap {
template <typename Offset>
class MapOrSetAllocationsTagger : public Allocations::Tagger<Offset> {
 public:
  typedef typename Allocations::Graph<Offset> Graph;
  typedef typename Allocations::Directory<Offset> Directory;
  typedef typename Allocations::Tagger<Offset> Tagger;
  typedef typename Tagger::Phase Phase;
  typedef typename Directory::AllocationIndex AllocationIndex;
  typedef typename Directory::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename TagHolder::TagIndex TagIndex;
  typedef typename Allocations::EdgePredicate<Offset> EdgePredicate;
  MapOrSetAllocationsTagger(Graph& graph, TagHolder& tagHolder,
                            EdgePredicate& edgeIsTainted,
                            EdgePredicate& edgeIsFavored)
      : _graph(graph),
        _tagHolder(tagHolder),
        _edgeIsTainted(edgeIsTainted),
        _edgeIsFavored(edgeIsFavored),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _addressMap(graph.GetAddressMap()),
        _nodeReader(_addressMap),
        _nodeTagIndex(_tagHolder.RegisterTag("%MapOrSetNode", true, true)) {}

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    /*
     * Note that we don't bother to check whether the allocation is unsigned
     * because only the least significant byte is set out of the first Offset,
     * meaning that a node in a map or set may give the illusion of being
     * signed.
     */
    return TagFromRootNode(contiguousImage, index, phase, allocation);
  }

  TagIndex GetNodeTagIndex() const { return _nodeTagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  EdgePredicate& _edgeIsTainted;
  EdgePredicate& _edgeIsFavored;
  const Directory& _directory;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  Reader _nodeReader;
  TagIndex _nodeTagIndex;
  Offset _parent;
  AllocationIndex _parentIndex;
  Offset _leftChild;
  Offset _rightChild;
  Offset _firstNode;
  Offset _lastNode;
  Offset _mapOrSetSize;

  bool TagFromRootNode(const ContiguousImage& contiguousImage,
                       AllocationIndex index, Phase phase,
                       const Allocation& allocation) {
    if (_tagHolder.IsStronglyTagged(index)) {
      /*
       * This was already strongly tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation is not the root
       * node for a map or set.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }

    Offset address = allocation.Address();
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        {
          const Offset* offsetLimit = contiguousImage.OffsetLimit();
          const Offset* firstOffset = contiguousImage.FirstOffset();
          if (((offsetLimit - firstOffset) < 5) ||
              ((firstOffset[0] & 0xfe) != 0)) {
            return true;
          }
          _parent = firstOffset[1];
          if (_parent == 0 || (_parent & (sizeof(Offset) - 1)) != 0) {
            return true;
          }
          _leftChild = firstOffset[2];
          if ((_leftChild & (sizeof(Offset) - 1)) != 0) {
            return true;
          }
          _rightChild = firstOffset[3];
          if ((_rightChild & (sizeof(Offset) - 1)) != 0) {
            return true;
          }
        }
        if ((_nodeReader.ReadOffset(_parent, 0xbad) & 0xfe) != 0) {
          return true;
        }
        if (address !=
            _nodeReader.ReadOffset(_parent + sizeof(Offset), 0xbad)) {
          return true;
        }
        _firstNode =
            _nodeReader.ReadOffset(_parent + 2 * sizeof(Offset), 0xbad);
        if (_firstNode == 0 || (_firstNode & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        _lastNode = _nodeReader.ReadOffset(_parent + 3 * sizeof(Offset), 0xbad);
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
            _nodeReader.ReadOffset(_parent + 4 * sizeof(Offset), 0xbad);
        if (_mapOrSetSize == 0) {
          return true;
        }
        if (_nodeReader.ReadOffset(_firstNode + 2 * sizeof(Offset), 0xbad) !=
            0) {
          return true;
        }
        if (_nodeReader.ReadOffset(_lastNode + 3 * sizeof(Offset), 0xbad) !=
            0) {
          return true;
        }
        _parentIndex = _graph.SourceAllocationIndex(index, _parent);
        if (_mapOrSetSize == 1) {
          if (_leftChild == 0 && _rightChild == 0) {
            // This is a trivial map or set of size 1.
            _tagHolder.TagAllocation(index, _nodeTagIndex);
            if (_parentIndex != _numAllocations) {
              _edgeIsFavored.Set(_parentIndex, index, true);
            }
          }
          return true;
        }
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        if (_mapOrSetSize <= 7) {
          CheckAllMapOrSetNodes(address, index);
          return true;
        }
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckAllMapOrSetNodes(address, index);
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
  void CheckAllMapOrSetNodes(Offset root, AllocationIndex rootIndex) {
    Offset numVisited = 0;
    Offset node = _firstNode;
    AllocationIndex firstNodeIndex =
        (node == root) ? rootIndex
                       : (_parentIndex == _numAllocations)
                             ? _directory.AllocationIndexOf(node)
                             : _graph.TargetAllocationIndex(_parentIndex, node);
    if (firstNodeIndex == _numAllocations) {
      return;
    }
    AllocationIndex nodeIndex = firstNodeIndex;
    Offset numLeftEdgesTraversed = 0;
    Offset numParentEdgesTraversed = 0;
    while (numVisited < _mapOrSetSize && node != _parent) {
      if ((_nodeReader.ReadOffset(node, 0xbad) & 0xfe) != 0) {
        return;
      }
      const Allocation* allocation = _directory.AllocationAt(nodeIndex);
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
          _nodeReader.ReadOffset(node + 3 * sizeof(Offset), 0xbad);
      if (rightChild != 0) {
        if ((rightChild & (sizeof(Offset) - 1)) != 0) {
          return;
        }
        AllocationIndex rightChildIndex =
            _graph.TargetAllocationIndex(nodeIndex, rightChild);
        if (rightChildIndex == _numAllocations) {
          return;
        }
        node = rightChild;
        nodeIndex = rightChildIndex;
        Offset leftChild =
            _nodeReader.ReadOffset(node + 2 * sizeof(Offset), 0xbad);
        ++numLeftEdgesTraversed;
        while (leftChild != 0) {
          AllocationIndex leftChildIndex =
              _graph.TargetAllocationIndex(nodeIndex, leftChild);
          if (leftChildIndex == _numAllocations) {
            return;
          }
          node = leftChild;
          nodeIndex = leftChildIndex;
          leftChild = _nodeReader.ReadOffset(node + 2 * sizeof(Offset), 0xbad);
          if (++numLeftEdgesTraversed > _mapOrSetSize) {
            return;
          }
        }
      } else {
        Offset parent = _nodeReader.ReadOffset(node + sizeof(Offset), 0xbad);
        ++numParentEdgesTraversed;
        while (parent != _parent &&
               _nodeReader.ReadOffset(parent + 3 * sizeof(Offset), 0xbad) ==
                   node) {
          AllocationIndex parentIndex =
              _graph.SourceAllocationIndex(nodeIndex, parent);
          node = parent;
          nodeIndex = parentIndex;
          parent = _nodeReader.ReadOffset(node + sizeof(Offset), 0xbad);
          if (++numParentEdgesTraversed > _mapOrSetSize) {
            return;
          }
        }
        AllocationIndex parentIndex = _numAllocations;
        if (parent != _parent) {
          if (_nodeReader.ReadOffset(parent + 2 * sizeof(Offset), 0xbad) !=
              node) {
            return;
          }
          parentIndex = _graph.SourceAllocationIndex(nodeIndex, parent);
          if (parentIndex == _numAllocations) {
            return;
          }
        }

        node = parent;
        nodeIndex = parentIndex;
      }
    }
    if (numVisited != _mapOrSetSize || node != _parent) {
      return;
    }

    node = _firstNode;
    nodeIndex = firstNodeIndex;
    while (node != _parent) {
      _tagHolder.TagAllocation(_directory.AllocationIndexOf(node),
                               _nodeTagIndex);

      Offset rightChild =
          _nodeReader.ReadOffset(node + 3 * sizeof(Offset), 0xbad);
      if (rightChild != 0) {
        AllocationIndex rightChildIndex =
            _graph.TargetAllocationIndex(nodeIndex, rightChild);
        _edgeIsFavored.Set(nodeIndex, rightChildIndex, true);
        node = rightChild;
        nodeIndex = rightChildIndex;
        Offset leftChild =
            _nodeReader.ReadOffset(node + 2 * sizeof(Offset), 0xbad);
        while (leftChild != 0) {
          AllocationIndex leftChildIndex =
              _graph.TargetAllocationIndex(nodeIndex, leftChild);
          node = leftChild;
          nodeIndex = leftChildIndex;
          leftChild = _nodeReader.ReadOffset(node + 2 * sizeof(Offset), 0xbad);
        }
      } else {
        Offset parent = _nodeReader.ReadOffset(node + sizeof(Offset), 0xbad);
        AllocationIndex parentIndex =
            _graph.SourceAllocationIndex(nodeIndex, parent);
        while ((parent != _parent) &&
               (_nodeReader.ReadOffset(parent + 3 * sizeof(Offset), 0xbad) ==
                node)) {
          node = parent;
          nodeIndex = parentIndex;
          parent = _nodeReader.ReadOffset(node + sizeof(Offset), 0xbad);
          parentIndex = _graph.SourceAllocationIndex(nodeIndex, parent);
        }
        if (parentIndex != _numAllocations) {
          _edgeIsFavored.Set(parentIndex, nodeIndex, true);
        }
        node = parent;
        nodeIndex = parentIndex;
      }
    }
  }
};
}  // namespace chap
