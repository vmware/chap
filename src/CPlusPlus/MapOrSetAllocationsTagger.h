// Copyright (c) 2019-2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/EdgePredicate.h"
#include "../Allocations/Graph.h"
#include "../Allocations/TagHolder.h"
#include "../Allocations/Tagger.h"
#include "../VirtualAddressMap.h"

namespace chap {
namespace CPlusPlus {
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
  static constexpr int MIN_NODE_SIZE_IN_OFFSETS = 5;
  static constexpr int MIN_NODE_SIZE =
      MIN_NODE_SIZE_IN_OFFSETS * sizeof(Offset);
  static constexpr int NUM_OFFSETS_BEFORE_PARENT = 1;
  static constexpr Offset PARENT_IN_NODE =
      NUM_OFFSETS_BEFORE_PARENT * sizeof(Offset);
  static constexpr int NUM_OFFSETS_BEFORE_LEFT_CHILD = 2;
  static constexpr Offset LEFT_CHILD_IN_NODE =
      NUM_OFFSETS_BEFORE_LEFT_CHILD * sizeof(Offset);
  static constexpr int NUM_OFFSETS_BEFORE_RIGHT_CHILD = 3;
  static constexpr Offset RIGHT_CHILD_IN_NODE =
      NUM_OFFSETS_BEFORE_RIGHT_CHILD * sizeof(Offset);
  static constexpr Offset ROOT_IN_PSEUDONODE = sizeof(Offset);
  static constexpr Offset FIRST_NODE_IN_PSEUDONODE = 2 * sizeof(Offset);
  static constexpr Offset LAST_NODE_IN_PSEUDONODE = 3 * sizeof(Offset);
  static constexpr Offset SIZE_IN_PSEUDONODE = 4 * sizeof(Offset);

  Graph& _graph;
  TagHolder& _tagHolder;
  EdgePredicate& _edgeIsTainted;
  EdgePredicate& _edgeIsFavored;
  const Directory& _directory;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  Reader _nodeReader;
  TagIndex _nodeTagIndex;
  Offset _pseudoNode;
  AllocationIndex _pseudoNodeIndex;
  Offset _leftChild;
  Offset _rightChild;
  Offset _firstNode;
  Offset _lastNode;
  bool _firstNodeVisited;
  bool _lastNodeVisited;
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
          if (((offsetLimit - firstOffset) < MIN_NODE_SIZE_IN_OFFSETS) ||
              ((firstOffset[0] & 0xfe) != 0)) {
            return true;
          }
          _pseudoNode = firstOffset[NUM_OFFSETS_BEFORE_PARENT];
          if (_pseudoNode == 0 || (_pseudoNode & (sizeof(Offset) - 1)) != 0) {
            return true;
          }
          _leftChild = firstOffset[NUM_OFFSETS_BEFORE_LEFT_CHILD];
          if ((_leftChild & (sizeof(Offset) - 1)) != 0) {
            return true;
          }
          _rightChild = firstOffset[NUM_OFFSETS_BEFORE_RIGHT_CHILD];
          if ((_rightChild & (sizeof(Offset) - 1)) != 0) {
            return true;
          }
        }
        if ((_nodeReader.ReadOffset(_pseudoNode, 0xbad) & 0xfe) != 0) {
          return true;
        }
        if (address !=
            _nodeReader.ReadOffset(_pseudoNode + ROOT_IN_PSEUDONODE, 0xbad)) {
          return true;
        }
        _firstNode = _nodeReader.ReadOffset(
            _pseudoNode + FIRST_NODE_IN_PSEUDONODE, 0xbad);
        if (_firstNode == 0 || (_firstNode & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        _firstNodeVisited = false;
        _lastNode = _nodeReader.ReadOffset(
            _pseudoNode + LAST_NODE_IN_PSEUDONODE, 0xbad);
        if (_lastNode == 0 || (_lastNode & (sizeof(Offset) - 1)) != 0) {
          return true;
        }
        _lastNodeVisited = false;
        if ((_leftChild == 0) != (_firstNode == address)) {
          return true;
        }
        if ((_rightChild == 0) != (_lastNode == address)) {
          return true;
        }
        _mapOrSetSize =
            _nodeReader.ReadOffset(_pseudoNode + SIZE_IN_PSEUDONODE, 0xbad);
        if (_mapOrSetSize == 0) {
          return true;
        }
        if (_nodeReader.ReadOffset(_firstNode + LEFT_CHILD_IN_NODE, 0xbad) !=
            0) {
          return true;
        }
        if (_nodeReader.ReadOffset(_lastNode + RIGHT_CHILD_IN_NODE, 0xbad) !=
            0) {
          return true;
        }
        _pseudoNodeIndex = _graph.SourceAllocationIndex(index, _pseudoNode);
        if (_mapOrSetSize == 1) {
          if (_leftChild == 0 && _rightChild == 0) {
            // This is a trivial map or set of size 1.
            _tagHolder.TagAllocation(index, _nodeTagIndex);
            if (_pseudoNodeIndex != _numAllocations) {
              _edgeIsFavored.Set(_pseudoNodeIndex, index, true);
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

  bool CheckChildAndDescendants(Offset node, AllocationIndex nodeIndex,
                                Offset child, Offset& numVisited, int depth) {
    if (child == 0) {
      return true;
    }
    if ((child & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    AllocationIndex childIndex = _graph.TargetAllocationIndex(nodeIndex, child);
    if (childIndex == _numAllocations) {
      return false;
    }
    return CheckNodeAndDescendants(child, childIndex, node, numVisited, depth);
  }

  bool CheckNodeAndDescendants(Offset node, AllocationIndex nodeIndex,
                               Offset parent, Offset& numVisited, int depth) {
    if (node == _firstNode) {
      _firstNodeVisited = true;
    }
    if (node == _lastNode) {
      _lastNodeVisited = true;
    }
    if (depth == sizeof(Offset) * 16) {
      return false;
    }
    if (++numVisited > _mapOrSetSize) {
      return false;
    }
    const Allocation* allocation = _directory.AllocationAt(nodeIndex);
    if (allocation == nullptr) {
      return false;
    }
    if (allocation->Size() < MIN_NODE_SIZE) {
      return false;
    }
    if (allocation->Address() != node) {
      return false;
    }
    if (_nodeReader.ReadOffset(node + PARENT_IN_NODE, 0xbad) != parent) {
      return false;
    }
    if ((_nodeReader.ReadOffset(node, 0xbad) & 0xfe) != 0) {
      return false;
    }
    return CheckChildAndDescendants(
               node, nodeIndex,
               _nodeReader.ReadOffset(node + LEFT_CHILD_IN_NODE, 0xbad),
               numVisited, depth + 1) &&
           CheckChildAndDescendants(
               node, nodeIndex,
               _nodeReader.ReadOffset(node + RIGHT_CHILD_IN_NODE, 0xbad),
               numVisited, depth + 1);
  }

  void TagNodeAndDescendants(Offset node, AllocationIndex nodeIndex,
                             AllocationIndex parentIndex) {
    _tagHolder.TagAllocation(nodeIndex, _nodeTagIndex);
    if (parentIndex != _numAllocations) {
      _edgeIsFavored.Set(parentIndex, nodeIndex, true);
    }
    Offset leftChild = _nodeReader.ReadOffset(node + LEFT_CHILD_IN_NODE, 0);
    if (leftChild != 0) {
      TagNodeAndDescendants(leftChild,
                            _graph.TargetAllocationIndex(nodeIndex, leftChild),
                            nodeIndex);
    }
    Offset rightChild = _nodeReader.ReadOffset(node + RIGHT_CHILD_IN_NODE, 0);
    if (rightChild != 0) {
      TagNodeAndDescendants(rightChild,
                            _graph.TargetAllocationIndex(nodeIndex, rightChild),
                            nodeIndex);
    }
  }

  void CheckAllMapOrSetNodes(Offset root, AllocationIndex rootIndex) {
    Offset numVisited = 0;
    if (CheckNodeAndDescendants(root, rootIndex, _pseudoNode, numVisited, 0) &&
        numVisited == _mapOrSetSize && _firstNodeVisited && _lastNodeVisited) {
      TagNodeAndDescendants(root, rootIndex, _pseudoNodeIndex);
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
