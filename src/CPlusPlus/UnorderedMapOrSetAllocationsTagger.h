// Copyright (c) 2019-2023 VMware, Inc. All Rights Reserved.
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
class UnorderedMapOrSetAllocationsTagger : public Allocations::Tagger<Offset> {
 public:
  /*
   * This is used to allow skipping forward a bit for the case where we
   * just found an embedded unordered map or set header and don't want
   * to scan.  It doesn't have to be accurate, as long as it is not too
   * large.  Some builds have space at the end of the header for the a
   * single bucket in case that is all that is needed.
   */
  static constexpr int MIN_OFFSETS_IN_HEADER = 6;
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
  typedef typename Allocations::EdgePredicate<Offset> EdgePredicate;
  UnorderedMapOrSetAllocationsTagger(Graph& graph, TagHolder& tagHolder,
                                     EdgePredicate& edgeIsTainted,
                                     EdgePredicate& edgeIsFavored)
      : _graph(graph),
        _tagHolder(tagHolder),
        _edgeIsTainted(edgeIsTainted),
        _edgeIsFavored(edgeIsFavored),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _addressMap(graph.GetAddressMap()),
        _staticAnchorReader(_addressMap),
        _stackAnchorReader(_addressMap),
        _nodeReader(_addressMap),
        _bucketsReader(_addressMap),
        _bucketsTagIndex(
            _tagHolder.RegisterTag("%UnorderedMapOrSetBuckets", true, true)),
        _nodeTagIndex(
            _tagHolder.RegisterTag("%UnorderedMapOrSetNode", true, true)) {}

  bool TagFromAllocation(const ContiguousImage& contiguousImage, Reader& reader,
                         AllocationIndex index, Phase phase,
                         const Allocation& allocation, bool isUnsigned) {
    if (!isUnsigned) {
      return true;
    }

    /*
     * Most non-empty unordered maps or unordered sets will have a buckets
     * array allocated outside the header.  In such a case, the most
     * efficient
     * way to find the nodes is to find the header by finding the buckets
     * array
     * then tag both the buckets array and nodes accordingly.
     */
    if (_tagHolder.IsStronglyTagged(index)) {
      /*
       * This was already strongly tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation is not a buckets
       * array or first item.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        /*
         * We can't be picky here because we are looking to match two possible
         * things.  One is a buckets array for an unordered set or map.  The
         * other is the first item on the list for an unordered set or map that
         * has no buckets array.
         */
        {
          const Offset* firstOffset = contiguousImage.FirstOffset();
          const Offset* offsetLimit = contiguousImage.OffsetLimit();
          if (offsetLimit - firstOffset < 2) {
            return true;
          }
          return (*firstOffset & (sizeof(Offset) - 1)) != 0;
        }
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        {
          Offset size = allocation.Size();
          Offset address = allocation.Address();
          if (!CheckAnchors(reader, _staticAnchorReader,
                            _graph.GetStaticAnchors(index), index, address,
                            size)) {
            CheckAnchors(reader, _stackAnchorReader,
                         _graph.GetStackAnchors(index), index, address, size);
          }
        }
        return true;
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        break;
      case Tagger::WEAK_CHECK:
        // May be expensive, weak results OK
        // An example here might be if one of the nodes in the chain is no
        // longer allocated.
        break;
    }
    return false;
  }

  bool TagFromReferenced(const ContiguousImage& contiguousImage, Reader& reader,
                         AllocationIndex index, Phase phase,
                         const Allocation& allocation,
                         const AllocationIndex* unresolvedOutgoing) {
    /*
     * In the more rare case that the maximum load factor is greater than
     * one,
     * and the number of allocations is sufficiently small that an
     * internal
     * single-bucket array in the header can be used, we can search for
     * the
     * first entries on the list for each unordered map or unordered set,
     * then
     * traverse the list to find the rest.  This is better done in the
     * second
     * pass, when nodes that can be found in the first pass have already
     * all
     * been tagged.
     */
    Offset size = allocation.Size();
    Offset address = allocation.Address();
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        return (size < 7 * sizeof(Offset));
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckEmbeddedUnorderedMapsOrSets(contiguousImage, reader, index,
                                         address, unresolvedOutgoing);
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

  TagIndex GetBucketsTagIndex() const { return _bucketsTagIndex; }
  TagIndex GetNodeTagIndex() const { return _nodeTagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  EdgePredicate& _edgeIsTainted;
  EdgePredicate& _edgeIsFavored;
  const Directory& _directory;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  Reader _staticAnchorReader;
  Reader _stackAnchorReader;
  Reader _nodeReader;
  Reader _bucketsReader;
  TagIndex _bucketsTagIndex;
  TagIndex _nodeTagIndex;

  bool CheckUnorderedMapOrSet(Offset unorderedMapOrSet,
                              AllocationIndex unorderedMapOrSetHolderIndex,
                              Reader& unorderedMapOrSetReader,
                              AllocationIndex bucketsIndex,
                              Reader& bucketsReader, Offset bucketsAddress,
                              Offset firstNodeAddress, Offset minBuckets,
                              Offset maxBuckets, bool expectEmpty) {
    if (unorderedMapOrSetReader.ReadOffset(unorderedMapOrSet, 0xbad) !=
        bucketsAddress) {
      return false;
    }
    Offset numBuckets = unorderedMapOrSetReader.ReadOffset(
        unorderedMapOrSet + sizeof(Offset), 0xbad);
    if (minBuckets > numBuckets || numBuckets > maxBuckets) {
      return false;
    }
    Offset firstNode = unorderedMapOrSetReader.ReadOffset(
        unorderedMapOrSet + 2 * sizeof(Offset), 0xbad);

    if (expectEmpty != (firstNode == 0) ||
        (firstNodeAddress != 0 && firstNodeAddress != firstNode)) {
      return false;
    }

    Offset numEntries = unorderedMapOrSetReader.ReadOffset(
        unorderedMapOrSet + 3 * sizeof(Offset), 0xbad);
    if (expectEmpty != (numEntries == 0)) {
      return false;
    }
    if (numEntries > (numBuckets * 4)) {
      /*
       * We expect the load factor to be less than 4 and we need to bound
       * numEntries so that the loop to check the chain length is
       * reasonably bounded.
       */
      return false;
    }

    uint32_t floatAsUint = unorderedMapOrSetReader.ReadU32(
        unorderedMapOrSet + 4 * sizeof(Offset), 0xbad);
    Offset threshold = unorderedMapOrSetReader.ReadOffset(
        unorderedMapOrSet + 5 * sizeof(Offset), 0);
    if (floatAsUint == 0x3f800000) {
      // The default load factor (1.0) applies.  We expect the threshold to
      // match the number of buckets.
      if (numBuckets != threshold) {
        return false;
      }
    } else if (floatAsUint < 0x3d800000 || floatAsUint > 0x41800000) {
      /*
       * Perhaps this is hackish, this is cheap to bound max load factor between
       * 1/16 and 4 as a way of verifying that this looks like an
       * unordered set or map.  It works because IEEE 754 32 bit format
       * has the most significant bit as a sign, followed by 8 bits of
       * exponent and there is an implicit 1 in the mantissa where the
       * left-most bit actually present in the mantissa represents 1/2.
       */
      return false;
    }

    AllocationIndex firstNodeIndex = _numAllocations;
    if (!expectEmpty) {
      if ((firstNode & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      if (numEntries > threshold) {
        return false;
      }
      Offset node = firstNode;
      Offset numVisited = 0;
      for (; node != 0 && numVisited < numEntries; ++numVisited) {
        node = _nodeReader.ReadOffset(node, 0);
      }
      if (numVisited < numEntries || node != 0) {
        return false;
      }

      firstNodeIndex = (unorderedMapOrSetHolderIndex == _numAllocations)
                           ? _directory.AllocationIndexOf(firstNode)
                           : _graph.TargetAllocationIndex(
                                 unorderedMapOrSetHolderIndex, firstNode);
      node = firstNode;
      AllocationIndex nodeIndex = firstNodeIndex;
      while (node != 0) {
        if (nodeIndex == _numAllocations) {
          return false;
        }
        node = _nodeReader.ReadOffset(node, 0);
        nodeIndex = _graph.TargetAllocationIndex(nodeIndex, node);
      }
    }
    if (bucketsAddress != unorderedMapOrSet + 6 * sizeof(Offset)) {
      /*
       * We have to check that an external buckets array at least appears
       * sane because the destructor for an unordered_set or unordered_map
       * leaves it well formed, and so a dynamically allocated object that
       * contained one of those could be destroyed and freed but still leave
       * residue of the unordered_map or unordered_set that wouldn't be
       * clobbered by the next malloc and depending on the new use of the
       * allocation might never be overwritten.
       */
      Offset bucketsLimit = bucketsAddress + (numBuckets * sizeof(Offset));
      Offset listHeader = unorderedMapOrSet + (2 * sizeof(Offset));
      for (Offset bucket = bucketsAddress; bucket < bucketsLimit;
           bucket += sizeof(Offset)) {
        Offset ppNodes = bucketsReader.ReadOffset(bucket, 0xbad);
        if (ppNodes != 0) {
          if (firstNode == 0) {
            return false;
          }
          if ((ppNodes & (sizeof(Offset) - 1)) != 0) {
            return false;
          }
          if ((ppNodes != listHeader) &&
              (_graph.TargetAllocationIndex(bucketsIndex, ppNodes) ==
               _numAllocations)) {
            return false;
          }
        }
      }
      _tagHolder.TagAllocation(bucketsIndex, _bucketsTagIndex);
      if (unorderedMapOrSetHolderIndex != _numAllocations) {
        _edgeIsFavored.Set(unorderedMapOrSetHolderIndex, bucketsIndex, true);
      }
    }

    Offset node = firstNode;
    AllocationIndex nodeIndex = firstNodeIndex;
    AllocationIndex refIndex = unorderedMapOrSetHolderIndex;
    while (node != 0) {
      if (!_tagHolder.TagAllocation(nodeIndex, _nodeTagIndex)) {
        std::cerr << "Warning: failed to tag allocation at 0x" << std::hex
                  << node << " as %UnorderedMapOrSetNode."
                             "\n   It was already tagged as "
                  << _tagHolder.GetTagName(nodeIndex) << "\n";
      }
      if (refIndex != _numAllocations) {
        _edgeIsFavored.Set(refIndex, nodeIndex, true);
      }
      refIndex = nodeIndex;
      node = _nodeReader.ReadOffset(node, 0);
      nodeIndex = _graph.TargetAllocationIndex(nodeIndex, node);
    }
    return true;
  }

  void CheckEmbeddedUnorderedMapsOrSets(
      const ContiguousImage& contiguousImage, Reader& reader,
      AllocationIndex unorderedMapOrSetHolderIndex, Offset address,
      const AllocationIndex* unresolvedOutgoing) {
    const Offset* checkLimit = contiguousImage.OffsetLimit() - 6;
    const Offset* firstCheck = contiguousImage.FirstOffset();

    for (const Offset* check = firstCheck; check < checkLimit; check++) {
      uint32_t floatAsUint = *((uint32_t*)(check + 4));
      if (floatAsUint < 0x3d800000 || floatAsUint > 0x41800000) {
        /*
         * Perhaps this is hackish, this is cheap to bound max load factor
         * between 1/16 and 4 as a way of verifying that this looks like an
         * unordered set or map.  It works because IEEE 754 32 bit format
         * has the most significant bit as a sign, followed by 8 bits of
         * exponent and there is an implicit 1 in the mantissa where the
         * left-most bit actually present in the mantissa represents 1/2.
         */
        continue;
      }
      Offset unorderedMapOrSetAddress =
          address + ((check - firstCheck) * sizeof(Offset));
      Offset bucketsAddress = check[0];
      Offset numBuckets = check[1];
      Offset firstNodeAddress = check[2];
      Offset numMembers = check[3];
      bool internalBuckets =
          (bucketsAddress == unorderedMapOrSetAddress + 6 * sizeof(Offset));
      AllocationIndex bucketsIndex = _numAllocations;
      Offset minBuckets = 1;
      Offset maxBuckets = 1;
      if (internalBuckets) {
        if (numBuckets != 1) {
          continue;
        }
        if (check[6] != unorderedMapOrSetAddress + 2 * sizeof(Offset)) {
          continue;
        }
      } else {
        bucketsIndex = unresolvedOutgoing[check - firstCheck];
        if (bucketsIndex == _numAllocations) {
          continue;
        }
        if (_tagHolder.GetTagIndex(bucketsIndex) != 0) {
          continue;
        }
        const Allocation* bucketsAllocation =
            _directory.AllocationAt(bucketsIndex);
        if (bucketsAllocation->Address() != bucketsAddress) {
          continue;
        }
        maxBuckets = bucketsAllocation->Size() / sizeof(Offset);
        if (numBuckets > maxBuckets) {
          continue;
        }
        minBuckets = _directory.MinRequestSize(bucketsIndex) / sizeof(Offset);
        if (minBuckets < 1) {
          minBuckets = 1;
        }
        if (numBuckets < minBuckets) {
          continue;
        }
      }
      if (firstNodeAddress == 0) {
        if (internalBuckets) {
          /*
           * There is nothing to tag if the buckets array is internal and
           * there are no elements, because we have only the header in
           * this case and that header is embedded in a larger allocation.
           */
          continue;
        }
        if (numMembers != 0) {
          continue;
        }
      } else {
        AllocationIndex firstNodeIndex =
            unresolvedOutgoing[(check - firstCheck) + 2];
        if (firstNodeIndex == _numAllocations) {
          continue;
        }
        if (_tagHolder.GetTagIndex(firstNodeIndex) != 0) {
          continue;
        }
        if (firstNodeAddress !=
            _directory.AllocationAt(firstNodeIndex)->Address()) {
          continue;
        }
        if (numMembers == 0) {
          continue;
        }
      }
      if (CheckUnorderedMapOrSet(
              unorderedMapOrSetAddress, unorderedMapOrSetHolderIndex, reader,
              bucketsIndex, _bucketsReader, bucketsAddress, firstNodeAddress,
              minBuckets, maxBuckets, firstNodeAddress == 0)) {
        check += (MIN_OFFSETS_IN_HEADER - 1);
      }
    }
  }

  bool CheckAnchors(Reader& bucketsReader, Reader& anchorReader,
                    const std::vector<Offset>* anchors, AllocationIndex index,
                    Offset address, Offset size) {
    if (anchors != nullptr) {
      for (Offset anchor : *anchors) {
        if (anchorReader.ReadOffset(anchor, 0xbad) != address) {
          continue;
        }
        /*
         * Check first to see whether the given allocation is a buckets array
         * anchor-point.
         */
        Offset firstNode =
            anchorReader.ReadOffset(anchor + 2 * sizeof(Offset), 0xbad);
        if ((firstNode & (sizeof(Offset) - 1)) == 0) {
          Offset maxBuckets = size / sizeof(Offset);
          Offset minBuckets = _directory.MinRequestSize(index) / sizeof(Offset);
          if (minBuckets < 1) {
            minBuckets = 1;
          }
          if (CheckUnorderedMapOrSet(anchor, _numAllocations, anchorReader,
                                     index, bucketsReader, address, firstNode,
                                     minBuckets, maxBuckets, firstNode == 0)) {
            return true;
          }
        }
        /*
         * Now check whether the allocation is an anchor point first node for
         * an unordered map or set that has an internal buckets array.
         */
        Offset unorderedMapOrSet = anchor - 2 * sizeof(Offset);
        Offset buckets = anchor + 4 * sizeof(Offset);
        if ((anchorReader.ReadOffset(unorderedMapOrSet, 0xbad) == buckets) &&
            CheckUnorderedMapOrSet(unorderedMapOrSet, _numAllocations,
                                   anchorReader, _numAllocations, anchorReader,
                                   buckets, address, 1, 1, false)) {
          return true;
        }
      }
    }
    return false;
  }
};
}  // namespace CPlusPlus
}  // namespace chap
