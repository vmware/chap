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
class UnorderedMapOrSetAllocationsTagger : public Allocations::Tagger<Offset> {
 public:
  typedef typename Allocations::Graph<Offset> Graph;
  typedef typename Allocations::Finder<Offset> Finder;
  typedef typename Allocations::Tagger<Offset> Tagger;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename Tagger::Phase Phase;
  typedef typename Finder::AllocationIndex AllocationIndex;
  typedef typename Finder::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename TagHolder::TagIndex TagIndex;
  UnorderedMapOrSetAllocationsTagger(Graph& graph, TagHolder& tagHolder)
      : _graph(graph),
        _tagHolder(tagHolder),
        _finder(graph.GetAllocationFinder()),
        _numAllocations(_finder.NumAllocations()),
        _addressMap(_finder.GetAddressMap()),
        _staticAnchorReader(_addressMap),
        _stackAnchorReader(_addressMap),
        _bucketsTagIndex(
            _tagHolder.RegisterTag("unordered set or map buckets")),
        _nodeTagIndex(_tagHolder.RegisterTag("unordered set or map node")) {}

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool isUnsigned) {
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
    if (_tagHolder.GetTagIndex(index) != 0) {
      /*
       * This was already tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation is not a buckets
       * array.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }

    Offset size = allocation.Size();
    Offset address = allocation.Address();
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        /*
         * We can't be picky here because we are looking to match two possible
         * things.  One is a buckets array for an unordered set or map.  The
         * other is the first item on the list for an unordered set or map that
         * has no buckets array.
         */
        return (size < 2 * sizeof(Offset));
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        if ((size <= 5 * sizeof(Offset)) &&
            CheckByPointerToMapOrSet(contiguousImage, index, address)) {
          return true;
        }
        if (CheckFirstNodeAnchors(_staticAnchorReader,
                                  _graph.GetStaticAnchors(index), index,
                                  address) ||
            CheckFirstNodeAnchors(_stackAnchorReader,
                                  _graph.GetStackAnchors(index), index,
                                  address)) {
          return true;
        }
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        return ((size > 5 * sizeof(Offset)) &&
                CheckByPointerToMapOrSet(contiguousImage, index, address)) ||
               CheckByReferenceFromEmptyMapOrSet(contiguousImage, index,
                                                 address, size);
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
  const Finder& _finder;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  Reader _staticAnchorReader;
  Reader _stackAnchorReader;
  TagIndex _bucketsTagIndex;
  TagIndex _nodeTagIndex;

  bool CheckByPointerToMapOrSet(const ContiguousImage& contiguousImage,
                                AllocationIndex index, Offset address) {
    Reader otherReader(_addressMap);

    const Offset* checkLimit = contiguousImage.OffsetLimit();
    const Offset* firstCheck = contiguousImage.FirstOffset();
    Offset maxBuckets = checkLimit - firstCheck;
    Offset minBuckets = (maxBuckets < 5) ? 1 : (maxBuckets - 4);

    for (const Offset* check = firstCheck; check < checkLimit; check++) {
      Offset o = *check;
      if (o == 0) {
        continue;
      }
      if ((o & (sizeof(Offset) - 1)) != 0) {
        break;
      }
      Offset mapOrSetCandidate = o - (2 * sizeof(Offset));
      if (CheckUnorderedMapOrSet(mapOrSetCandidate, _numAllocations,
                                 otherReader, index, address, 0, minBuckets,
                                 maxBuckets, false)) {
        return true;
      }
      // TODO: One could be more rigorous here by checking all the buckets
      // that were not yet checked.
      // TODO: In the case that the unordered map or unordered set is in flux
      // the list length might reasonably not match the count.
    }
    return false;
  }
  bool CheckByReferenceFromEmptyMapOrSet(const ContiguousImage& contiguousImage,
                                         AllocationIndex index, Offset address,
                                         Offset size) {
    Offset maxStartingEmptyBuckets = 0;

    const Offset* checkLimit = contiguousImage.OffsetLimit();
    const Offset* firstCheck = contiguousImage.FirstOffset();
    for (const Offset* check = firstCheck; check < checkLimit; check++) {
      if (*check != 0) {
        break;
      }
      maxStartingEmptyBuckets++;
    }
    Reader otherReader(_addressMap);
    if (maxStartingEmptyBuckets * sizeof(Offset) >= size / 2) {
      const AllocationIndex* pFirstIncoming;
      const AllocationIndex* pPastIncoming;
      _graph.GetIncoming(index, &pFirstIncoming, &pPastIncoming);
      for (const AllocationIndex* pNextIncoming = pFirstIncoming;
           pNextIncoming != pPastIncoming; ++pNextIncoming) {
        const Allocation* incomingAllocation =
            _finder.AllocationAt(*pNextIncoming);
        Offset incomingSize = incomingAllocation->Size();
        if (incomingSize < 7 * sizeof(Offset)) {
          continue;
        }
        Offset checkAt = incomingAllocation->Address();
        Offset checkLimit = checkAt + (incomingSize & ~(sizeof(Offset) - 1)) -
                            6 * sizeof(Offset);
        for (; checkAt < checkLimit; checkAt += sizeof(Offset)) {
          if (CheckUnorderedMapOrSet(checkAt, _numAllocations, otherReader,
                                     index, address, 0, 1,
                                     maxStartingEmptyBuckets, true)) {
            return true;
          }
        }
      }
    }
    return CheckAnchors(_graph.GetStaticAnchors(index), otherReader, index,
                        address, 1, maxStartingEmptyBuckets) ||
           CheckAnchors(_graph.GetStackAnchors(index), otherReader, index,
                        address, 1, maxStartingEmptyBuckets);
    return false;
  }

  bool CheckAnchors(const std::vector<Offset>* anchors,
                    Reader& unorderedMapOrSetReader,
                    AllocationIndex bucketsIndex, Offset bucketsAddress,
                    Offset minBuckets, Offset maxBuckets) {
    if (anchors != nullptr) {
      for (Offset anchor : *anchors) {
        if (CheckUnorderedMapOrSet(
                anchor, _numAllocations, unorderedMapOrSetReader, bucketsIndex,
                bucketsAddress, 0, minBuckets, maxBuckets, true)) {
          return true;
        }
      }
    }
    return false;
  }

  bool CheckUnorderedMapOrSet(Offset mapOrSet, AllocationIndex mapOrSetIndex,
                              Reader& unorderedMapOrSetReader,
                              AllocationIndex bucketsIndex,
                              Offset bucketsAddress, Offset firstNodeAddress,
                              Offset minBuckets, Offset maxBuckets,
                              bool expectEmpty) {
    if (unorderedMapOrSetReader.ReadOffset(mapOrSet, 0xbad) != bucketsAddress) {
      return false;
    }
    Offset numBuckets =
        unorderedMapOrSetReader.ReadOffset(mapOrSet + sizeof(Offset), 0xbad);
    if (minBuckets > numBuckets || numBuckets > maxBuckets) {
      return false;
    }
    Offset firstNode = unorderedMapOrSetReader.ReadOffset(
        mapOrSet + 2 * sizeof(Offset), 0xbad);

    if (expectEmpty != (firstNode == 0) ||
        (firstNodeAddress != 0 && firstNodeAddress != firstNode)) {
      return false;
    }

    Offset numEntries = unorderedMapOrSetReader.ReadOffset(
        mapOrSet + 3 * sizeof(Offset), 0xbad);
    if (expectEmpty != (numEntries == 0)) {
      return false;
    }

    uint32_t floatAsUint =
        unorderedMapOrSetReader.ReadU32(mapOrSet + 4 * sizeof(Offset), 0xbad);
    Offset threshold =
        unorderedMapOrSetReader.ReadOffset(mapOrSet + 5 * sizeof(Offset), 0);
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

    Reader nodeReader(_addressMap);
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
        node = nodeReader.ReadOffset(node, 0);
      }
      if (numVisited < numEntries || node != 0) {
        return false;
      }

      firstNodeIndex =
          (mapOrSetIndex == _numAllocations)
              ? _finder.AllocationIndexOf(firstNode)
              : _graph.TargetAllocationIndex(mapOrSetIndex, firstNode);
      node = firstNode;
      AllocationIndex nodeIndex = firstNodeIndex;
      while (node != 0) {
        if (nodeIndex == _numAllocations) {
          return false;
        }
        node = nodeReader.ReadOffset(node, 0);
        nodeIndex = _graph.TargetAllocationIndex(nodeIndex, node);
      }

      // TODO: We could check the entries in the buckets.  With the exception
      // of the one that points back to the list header in the map or set,
      // should all be 0 or point to valid allocations.
    }
    if (bucketsAddress != mapOrSet + 6 * sizeof(Offset)) {
      _tagHolder.TagAllocation(bucketsIndex, _bucketsTagIndex);
    }

    Offset node = firstNode;
    AllocationIndex nodeIndex = firstNodeIndex;
    while (node != 0) {
      _tagHolder.TagAllocation(nodeIndex, _nodeTagIndex);
      node = nodeReader.ReadOffset(node, 0);
      nodeIndex = _graph.TargetAllocationIndex(nodeIndex, node);
    }
    return true;
  }

  void CheckEmbeddedUnorderedMapsOrSets(
      const ContiguousImage& contiguousImage, Reader& reader,
      AllocationIndex mapOrSetIndex, Offset address,
      const AllocationIndex* unresolvedOutgoing) {
    const Offset* checkLimit = contiguousImage.OffsetLimit() - 6;
    const Offset* firstCheck = contiguousImage.FirstOffset();

    for (const Offset* check = firstCheck; check < checkLimit; check++) {
      Offset dequeAddress = address + ((check - firstCheck) * sizeof(Offset));
      Offset bucketsAddress = check[0];
      Offset numBuckets = check[1];
      bool internalBuckets =
          (bucketsAddress == dequeAddress + 6 * sizeof(Offset));
      AllocationIndex bucketsIndex = _numAllocations;
      if (internalBuckets) {
        if (numBuckets != 1) {
          continue;
        }
        if (check[6] != dequeAddress + 2 * sizeof(Offset)) {
          continue;
        }
      } else {
        if (bucketsIndex > 0) {
          // ??? TB debug hack to avoid the case of the external map
          // ??? with an empty set.
          continue;
        }
        bucketsIndex = unresolvedOutgoing[check - firstCheck];
        if (bucketsIndex == _numAllocations) {
          continue;
        }
        if (_tagHolder.GetTagIndex(bucketsIndex) != 0) {
          continue;
        }
      }
      Offset firstNodeAddress = check[2];
      Offset numMembers = check[3];
      if (firstNodeAddress == 0) {
        if (internalBuckets) {
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
            _finder.AllocationAt(firstNodeIndex)->Address()) {
          continue;
        }
        if (numMembers == 0) {
          continue;
        }
      }
      if (CheckUnorderedMapOrSet(dequeAddress, mapOrSetIndex, reader,
                                 bucketsIndex, bucketsAddress, firstNodeAddress,
                                 numBuckets, numBuckets,
                                 firstNodeAddress == 0)) {
        check += 6;
      }
    }
  }

  bool CheckFirstNodeAnchors(Reader& anchorReader,
                             const std::vector<Offset>* anchors,
                             AllocationIndex index, Offset firstNodeAddress) {
    if (anchors != nullptr) {
      for (Offset anchor : *anchors) {
        Offset unorderedMapOrSet = anchor - 2 * sizeof(Offset);
        Offset buckets = anchor + 4 * sizeof(Offset);
        if ((anchorReader.ReadOffset(unorderedMapOrSet, 0xbad) == buckets) &&
            CheckUnorderedMapOrSet(unorderedMapOrSet, _numAllocations,
                                   anchorReader, index, buckets,
                                   firstNodeAddress, 1, 1, false)) {
          return true;
        }
      }
    }
    return false;
  }
};
}  // namespace chap
