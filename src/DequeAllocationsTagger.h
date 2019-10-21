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
class DequeAllocationsTagger : public Allocations::Tagger<Offset> {
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
  DequeAllocationsTagger(Graph& graph, TagHolder& tagHolder)
      : _graph(graph),
        _tagHolder(tagHolder),
        _finder(graph.GetAllocationFinder()),
        _numAllocations(_finder.NumAllocations()),
        _addressMap(_finder.GetAddressMap()),
        _mapTagIndex(_tagHolder.RegisterTag("unordered set or map buckets")),
        _blockTagIndex(_tagHolder.RegisterTag("unordered set or map node")) {}

  bool TagFromAllocation(Reader& reader, Pass pass, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    /*
     * Note that we cannot assume anything based on the start of a map
     * allocation because the start of the allocation is not initialized
     * eagerly, even if the middle has useful contents.  For this reason,
     * even though at some level we don't expect a signature, at some
     * point if we didn't happen to have a free() implemention that clobbers
     * the first Offset on free, we might have a residual signature there.
     * For this reason, it is better not to check isUnsigned at all.
     */
    switch (pass) {
      case Tagger::FIRST_PASS_THROUGH_ALLOCATIONS:
        return TagAnchorPointDequeMap(reader, index, phase, allocation);
        break;
      case Tagger::LAST_PASS_THROUGH_ALLOCATIONS:
        return TagFromContainedDeques(reader, index, phase, allocation);
        break;
    }
    /*
     * There is no need to look any more at this allocation during this pass.
     */
    return true;
  }

  TagIndex GetMapTagIndex() const { return _mapTagIndex; }
  TagIndex GetBlockTagIndex() const { return _blockTagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  const Finder& _finder;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  TagIndex _mapTagIndex;
  TagIndex _blockTagIndex;

  /*
   * Check whether the specified allocation is a deque map, where the deque
   * is on the stack or statically allocated, tagging it and any associated
   * deque blocks if so.  Return true if no further work is needed to check.
   */
  bool TagAnchorPointDequeMap(Reader& reader, AllocationIndex index,
                              Phase phase, const Allocation& allocation) {
    if (_tagHolder.GetTagIndex(index) != 0) {
      /*
       * This was already tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation is not a deque map.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }
    Offset address = allocation.Address();
    Offset size = allocation.Size();

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
        if (!CheckDequeMapAnchorIn(reader, index, allocation,
                                   _graph.GetStaticAnchors(index))) {
          CheckDequeMapAnchorIn(reader, index, allocation,
                                _graph.GetStackAnchors(index));
        }
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
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

  bool CheckDequeMapAnchorIn(Reader& mapReader, AllocationIndex mapIndex,
                             const Allocation& mapAllocation,
                             const std::vector<Offset>* anchors) {
    Offset mapAddress = mapAllocation.Address();
    if (anchors != nullptr) {
      typename VirtualAddressMap<Offset>::Reader dequeReader(_addressMap);
      for (Offset anchor : *anchors) {
        if (dequeReader.ReadOffset(anchor, 0xbad) != mapAddress) {
          continue;
        }
        if (TagAllocationsIfDeque(dequeReader, mapReader, mapIndex,
                                  &mapAllocation, anchor)) {
          return true;
        }
      }
    }
    return false;
  }

  bool TagAllocationsIfDeque(Reader& dequeReader, Reader& mapReader,
                             AllocationIndex mapIndex,
                             const Allocation* mapAllocation,
                             Offset dequeAddress) {
    Offset mapAddress = dequeReader.ReadOffset(dequeAddress, 0xbadbad);
    if (mapAllocation != nullptr) {
      /*
       * If we have a specific address in mind for the map, it must match
       * the address at the start of the deque.
       */
      if (mapAddress != mapAllocation->Address()) {
        return false;
      }
    } else {
      /*
       * If we don't have a specific address in mind for the map, do some
       * superficial check now but don't actually figure out the allocation
       * index yet because that is relatively expensive compared to other
       * checks that may exclude it.
       */
      if (mapAddress == 0 || (mapAddress & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
    }
    Offset maxEntries =
        dequeReader.ReadOffset(dequeAddress + sizeof(Offset), 0);
    if (maxEntries == 0) {
      return false;
    }

    Offset liveAreaLimit = mapAddress + maxEntries * sizeof(Offset);
    Offset startMNode =
        dequeReader.ReadOffset(dequeAddress + 5 * sizeof(Offset), 0xbad);
    if ((startMNode & (sizeof(Offset) - 1)) != 0 || startMNode < mapAddress ||
        startMNode >= liveAreaLimit) {
      return false;
    }
    Offset finishMNode =
        dequeReader.ReadOffset(dequeAddress + 9 * sizeof(Offset), 0xbad);
    if (finishMNode != startMNode &&
        ((finishMNode & (sizeof(Offset) - 1)) != 0 ||
         finishMNode < mapAddress || finishMNode >= liveAreaLimit)) {
      return false;
    }

    Offset startCur =
        dequeReader.ReadOffset(dequeAddress + 2 * sizeof(Offset), 0xbad);
    if (startCur == 0xbad) {
      return false;
    }
    Offset startFirst =
        dequeReader.ReadOffset(dequeAddress + 3 * sizeof(Offset), 0xbad);
    if (startFirst == 0xbad || startCur < startFirst) {
      return false;
    }
    Offset startLast =
        dequeReader.ReadOffset(dequeAddress + 4 * sizeof(Offset), 0xbad);
    if (startLast == 0xbad || startCur >= startLast) {
      return false;
    }
    Offset finishCur =
        dequeReader.ReadOffset(dequeAddress + 6 * sizeof(Offset), 0xbad);
    Offset finishFirst =
        dequeReader.ReadOffset(dequeAddress + 7 * sizeof(Offset), 0xbad);
    Offset finishLast =
        dequeReader.ReadOffset(dequeAddress + 8 * sizeof(Offset), 0xbad);
    if (finishMNode == startMNode) {
      if (startFirst != finishFirst || startLast != finishLast ||
          startCur > finishCur) {
        return false;
      }
    } else {
      if (finishCur == 0xbad || finishFirst == 0xbad || finishLast == 0xbad ||
          finishCur < finishFirst || finishCur >= finishLast) {
        return false;
      }
    }

    if (mapReader.ReadOffset(startMNode, 0xbad) != startFirst) {
      return false;
    }
    // TODO: check that startFirst starts an allocation of 0x200+ bytes
    if (startMNode != finishMNode) {
      if (mapReader.ReadOffset(finishMNode, 0xbad) != finishFirst) {
        return false;
      }
      // TODO: check that finishFirst starts an allocation of 0x200+ bytes
    }

    if (mapAllocation == nullptr) {
      /*
       * No particular allocation was given for the map.  Now that the cheaper
       * checks are done, it is reasonable to make sure that the mapAddress
       * found above actually corresponds to the start of a used allocation.
       */
      mapIndex = _finder.AllocationIndexOf(mapAddress);
      if (mapIndex == _numAllocations) {
        return false;
      }
      mapAllocation = _finder.AllocationAt(mapIndex);
      if (mapAllocation == nullptr) {
        return false;
      }
      if (mapAllocation->Address() != mapAddress) {
        return false;
      }
    }
    Offset maxMaxEntries = (mapAllocation->Size()) / sizeof(Offset);
    Offset minMaxEntries = (maxMaxEntries <= 9) ? 4 : (maxMaxEntries - 5);

    if (maxEntries == 0xbadbad || maxEntries > maxMaxEntries ||
        maxEntries < minMaxEntries) {
      return false;
    }

    for (Offset mNode = startMNode; mNode <= finishMNode;
         mNode += sizeof(Offset)) {
      Offset blockAddress = mapReader.ReadOffset(mNode, 0xbad);
      if ((blockAddress & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      AllocationIndex blockIndex = _finder.AllocationIndexOf(blockAddress);
      if (blockIndex == _numAllocations) {
        return false;
      }
      const Allocation* blockAllocation = _finder.AllocationAt(blockIndex);
      if (blockAllocation == nullptr) {
        return false;
      }
      if (blockAllocation->Address() != blockAddress) {
        return false;
      }
    }
    _tagHolder.TagAllocation(mapIndex, _mapTagIndex);
    for (Offset mNode = startMNode; mNode <= finishMNode;
         mNode += sizeof(Offset)) {
      _tagHolder.TagAllocation(
          _finder.AllocationIndexOf(mapReader.ReadOffset(mNode, 0)),
          _blockTagIndex);
    }
    return true;
  }

  /*
   * Check whether the specified allocation contains any deques.  If so,
   * tag the associated deque maps and any associated deque blocks.
   */
  bool TagFromContainedDeques(Reader& reader, AllocationIndex /*index*/,
                              Phase phase, const Allocation& allocation) {
    /*
     * We don't care about the index, because we aren't tagging the allocation
     * that contains the deques.
     */
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        return allocation.Size() < 10 * sizeof(Offset);
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        // ??? check all the deques in the allocation, if small
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckEmbeddedDeques(reader, allocation.Address(), allocation.Size());
        break;
      case Tagger::WEAK_CHECK:
        // May be expensive, weak results OK
        // An example here might be if one of the nodes in the chain is no
        // longer allocated.
        break;
    }
    return false;
  }

  void CheckEmbeddedDeques(Reader& dequeReader, Offset address, Offset size) {
    Reader mapReader(_addressMap);
    Offset checkLimit =
        address + (size & ~(sizeof(Offset) - 1)) - 9 * sizeof(Offset);
    for (Offset checkAt = address; checkAt < checkLimit;
         checkAt += sizeof(Offset)) {
      if (TagAllocationsIfDeque(dequeReader, mapReader, _numAllocations,
                                nullptr, checkAt)) {
        checkAt += 9 * sizeof(Offset);
      }
    }
  }
};
}  // namespace chap
