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
  typedef typename Tagger::Pass Pass;
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
        _bucketsTagIndex(
            _tagHolder.RegisterTag("unordered set or map buckets")),
        _nodeTagIndex(_tagHolder.RegisterTag("unordered set or map node")) {}

  bool TagFromAllocation(Reader& reader, Pass pass, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool isUnsigned) {
    if (isUnsigned) {
      switch (pass) {
        case Tagger::FIRST_PASS_THROUGH_ALLOCATIONS:
          /*
           * Most non-empty unordered maps or unordered sets will have a buckets
           * array allocated outside the header.  In such a case, the most
           * efficient
           * way to find the nodes is to find the header by finding the buckets
           * array
           * then tag both the buckets array and nodes accordingly.
           */
          return TagFromExternalBucketsArray(reader, index, phase, allocation);
          break;
        case Tagger::LAST_PASS_THROUGH_ALLOCATIONS:
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
          return TagFromFirstNodeOnList(reader, index, phase, allocation);
          break;
      }
    }
    /*
     * There is no need to look any more at this allocation during this pass.
     */
    return true;
  }

  TagIndex GetBucketsTagIndex() const { return _bucketsTagIndex; }
  TagIndex GetNodeTagIndex() const { return _nodeTagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  const Finder& _finder;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  TagIndex _bucketsTagIndex;
  TagIndex _nodeTagIndex;

  bool TagFromExternalBucketsArray(Reader& reader, AllocationIndex index,
                                   Phase phase, const Allocation& allocation) {
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
        return (size < 2 * sizeof(Offset)) ||
               ((reader.ReadOffset(address, 0xbad) & (sizeof(Offset) - 1)) !=
                0) ||
               ((reader.ReadOffset(address + sizeof(Offset), 0xbad) &
                 (sizeof(Offset) - 1)) != 0);
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        return (size <= 5 * sizeof(Offset)) &&
               CheckByPointerToMapOrSet(reader, index, address, size);
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        return ((size > 5 * sizeof(Offset)) &&
                CheckByPointerToMapOrSet(reader, index, address, size)) ||
               CheckByReferenceFromEmptyMapOrSet(reader, index, address, size);
        break;
      case Tagger::WEAK_CHECK:
        // May be expensive, weak results OK
        // An example here might be if one of the nodes in the chain is no
        // longer allocated.
        break;
    }
    return false;
  }

  bool CheckByPointerToMapOrSet(Reader& reader, AllocationIndex index,
                                Offset address, Offset size) {
    Reader otherReader(_addressMap);
    Offset checkAt = address;
    Offset checkLimit =
        address + ((size + sizeof(Offset) - 1) & ~(sizeof(Offset) - 1));
    Offset maxBuckets = size / sizeof(Offset);
    Offset minBuckets = (maxBuckets < 5) ? 1 : (maxBuckets - 4);
    for (; checkAt < checkLimit; checkAt += sizeof(Offset)) {
      Offset o = reader.ReadOffset(checkAt, 0xbad);
      if (o == 0) {
        continue;
      }
      if ((o & (sizeof(Offset) - 1)) != 0) {
        break;
      }
      Offset mapOrSetCandidate = o - (2 * sizeof(Offset));
      if (CheckMapOrSet(mapOrSetCandidate, otherReader, index, address, 0,
                        minBuckets, maxBuckets, false)) {
        return true;
      }
      // TODO: One could be more rigorous here by checking all the buckets
      // that were not yet checked.
      // TODO: In the case that the unordered map or unordered set is in flux
      // the list length might reasonably not match the count.
    }
    return false;
  }
  bool CheckByReferenceFromEmptyMapOrSet(Reader& reader, AllocationIndex index,
                                         Offset address, Offset size) {
    Offset maxStartingEmptyBuckets = 0;
    Offset checkLimit = address + (size & ~(sizeof(Offset) - 1));
    for (Offset checkAt = address; checkAt < checkLimit;
         checkAt += sizeof(Offset)) {
      if (reader.ReadOffset(checkAt, 0xbad) != 0) {
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
          if (CheckMapOrSet(checkAt, otherReader, index, address, 0, 1,
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

  bool CheckAnchors(const std::vector<Offset>* anchors, Reader& reader,
                    AllocationIndex index, Offset bucketsAddress,
                    Offset minBuckets, Offset maxBuckets) {
    if (anchors != nullptr) {
      for (Offset anchor : *anchors) {
        if (CheckMapOrSet(anchor, reader, index, bucketsAddress, 0, minBuckets,
                          maxBuckets, true)) {
          return true;
        }
      }
    }
    return false;
  }

  bool CheckMapOrSet(Offset mapOrSet, Reader& reader,
                     AllocationIndex bucketsIndex, Offset bucketsAddress,
                     Offset firstNodeAddress, Offset minBuckets,
                     Offset maxBuckets, bool expectEmpty) {
    if (reader.ReadOffset(mapOrSet, 0xbad) != bucketsAddress) {
      return false;
    }
    Offset numBuckets = reader.ReadOffset(mapOrSet + sizeof(Offset), 0xbad);
    if (minBuckets > numBuckets || numBuckets > maxBuckets) {
      return false;
    }
    Offset firstNodeCandidate =
        reader.ReadOffset(mapOrSet + 2 * sizeof(Offset), 0xbad);

    if (expectEmpty != (firstNodeCandidate == 0) ||
        (firstNodeAddress != 0 && firstNodeAddress != firstNodeCandidate)) {
      return false;
    }

    Offset numEntries = reader.ReadOffset(mapOrSet + 3 * sizeof(Offset), 0xbad);
    if (expectEmpty != (numEntries == 0)) {
      return false;
    }

    uint32_t floatAsUint = reader.ReadU32(mapOrSet + 4 * sizeof(Offset), 0xbad);
    Offset threshold = reader.ReadOffset(mapOrSet + 5 * sizeof(Offset), 0);
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

    if (!expectEmpty) {
      if ((firstNodeCandidate & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      if (numEntries > threshold) {
        return false;
      }
      Offset nodeCandidate = firstNodeCandidate;
      Offset numVisited = 0;
      for (; nodeCandidate != 0 && numVisited < numEntries; ++numVisited) {
        nodeCandidate = reader.ReadOffset(nodeCandidate, 0);
      }
      if (numVisited < numEntries || nodeCandidate != 0) {
        return false;
      }
      for (nodeCandidate = firstNodeCandidate; nodeCandidate != 0;
           nodeCandidate = reader.ReadOffset(nodeCandidate, 0)) {
        if (_finder.AllocationIndexOf(nodeCandidate) == _numAllocations) {
          return false;
        }
      }
      // TODO: We could check the entries in the buckets to, with the exception
      // of the one that points back to the list header in the map or set,
      // should
      // all be 0 or point to valid allocations.
    }
    if (bucketsAddress != mapOrSet + 6 * sizeof(Offset)) {
      _tagHolder.TagAllocation(bucketsIndex, _bucketsTagIndex);
    }
    for (Offset nodeCandidate = firstNodeCandidate; nodeCandidate != 0;
         nodeCandidate = reader.ReadOffset(nodeCandidate, 0)) {
      _tagHolder.TagAllocation(_finder.AllocationIndexOf(nodeCandidate),
                               _nodeTagIndex);
    }
    return true;
  }

  bool TagFromFirstNodeOnList(Reader& reader, AllocationIndex index,
                              Phase phase, const Allocation& allocation) {
    Offset size = allocation.Size();
    Offset address = allocation.Address();
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        return (size < 2 * sizeof(Offset)) ||
               ((reader.ReadOffset(address, 0xbad) & (sizeof(Offset) - 1)) !=
                0);
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        return CheckFirstNodeAnchors(_graph.GetStaticAnchors(index), index,
                                     address) ||
               CheckFirstNodeAnchors(_graph.GetStackAnchors(index), index,
                                     address);
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckEmbeddedSingleBucketUnorderedMapsOrSets(address, size);
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

  void CheckEmbeddedSingleBucketUnorderedMapsOrSets(Offset address,
                                                    Offset size) {
    Reader reader(_addressMap);
    Offset checkLimit =
        address + (size & ~(sizeof(Offset) - 1)) - 6 * sizeof(Offset);
    for (Offset checkAt = address; checkAt < checkLimit;
         checkAt += sizeof(Offset)) {
      if (reader.ReadOffset(checkAt, 0xbad) != checkAt + 6 * sizeof(Offset) ||
          reader.ReadOffset(checkAt + sizeof(Offset), 0xbad) != 1) {
        continue;
      }
      Offset firstNodeAddress =
          reader.ReadOffset(checkAt + 2 * sizeof(Offset), 0xbad);
      if (firstNodeAddress == 0 ||
          ((firstNodeAddress & (sizeof(Offset) - 1)) != 0)) {
        continue;
      }
      if (reader.ReadOffset(checkAt + 3 * sizeof(Offset), 0) == 0) {
        continue;
      }
      if (reader.ReadOffset(checkAt + 6 * sizeof(Offset), 0xbad) !=
          checkAt + 2 * sizeof(Offset)) {
      }
      CheckMapOrSet(checkAt, reader, _numAllocations,
                    checkAt + 6 * sizeof(Offset), firstNodeAddress, 1, 1,
                    false);
    }
  }
  bool CheckFirstNodeAnchors(const std::vector<Offset>* anchors,
                             AllocationIndex index, Offset firstNodeAddress) {
    Reader reader(_addressMap);
    if (anchors != nullptr) {
      for (Offset anchor : *anchors) {
        if (CheckMapOrSet(anchor - 2 * sizeof(Offset), reader, index,
                          anchor + 4 * sizeof(Offset), firstNodeAddress, 1, 1,
                          false)) {
          return true;
        }
      }
    }
    return false;
  }
};
}  // namespace chap
