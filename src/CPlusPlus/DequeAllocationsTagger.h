// Copyright (c) 2019-2022 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
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
class DequeAllocationsTagger : public Allocations::Tagger<Offset> {
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
  typedef typename Allocations::EdgePredicate<Offset> EdgePredicate;
  typedef typename TagHolder::TagIndex TagIndex;
  static constexpr int NUM_OFFSETS_IN_HEADER = 10;
  DequeAllocationsTagger(Graph& graph, TagHolder& tagHolder,
                         EdgePredicate& edgeIsTainted,
                         EdgePredicate& edgeIsFavored)
      : _graph(graph),
        _tagHolder(tagHolder),
        _edgeIsTainted(edgeIsTainted),
        _edgeIsFavored(edgeIsFavored),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _addressMap(graph.GetAddressMap()),
        _mapReader(_addressMap),
        _endIterator(_addressMap.end()),
        _anchorIterator(_addressMap.end()),
        _mapTagIndex(_tagHolder.RegisterTag("%DequeMap", true, true)),
        _blockTagIndex(_tagHolder.RegisterTag("%DequeBlock", true, true)) {}

  bool TagFromAllocation(const ContiguousImage& /* contiguousImage */,
                         Reader& reader, AllocationIndex index, Phase phase,
                         const Allocation& allocation, bool /* isUnsigned */) {
    /*
       * Note that we cannot assume anything based on the start of a map
       * allocation because the start of the allocation is not initialized
       * eagerly, even if the middle has useful contents.  For this reason,
       * even though at some level we don't expect a signature, at some
       * point if we didn't happen to have a free() implemention that clobbers
       * the first Offset on free, we might have a residual signature there.
       * For this reason, it is better not to check isUnsigned at all.
       */
    return TagAnchorPointDequeMap(reader, index, phase, allocation);
  }

  bool TagFromReferenced(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         const AllocationIndex* unresolvedOutgoing) {
    return TagFromContainedDeques(index, contiguousImage, phase, allocation,
                                  unresolvedOutgoing);
  }

  TagIndex GetMapTagIndex() const { return _mapTagIndex; }
  TagIndex GetBlockTagIndex() const { return _blockTagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  EdgePredicate& _edgeIsTainted;
  EdgePredicate& _edgeIsFavored;
  const Directory& _directory;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  Reader _mapReader;
  const typename VirtualAddressMap<Offset>::const_iterator _endIterator;
  typename VirtualAddressMap<Offset>::const_iterator _anchorIterator;
  TagIndex _mapTagIndex;
  TagIndex _blockTagIndex;

  /*
   * Check whether the specified allocation is a deque map, where the deque
   * is on the stack or statically allocated, tagging it and any associated
   * deque blocks if so.  Return true if no further work is needed to check.
   */
  bool TagAnchorPointDequeMap(Reader& reader, AllocationIndex index,
                              Phase phase, const Allocation& allocation) {
    if (_tagHolder.IsStronglyTagged(index)) {
      /*
       * This was already strongly tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation is not a deque map.
       * Note that in theory such an allocation could be weakly tagged,
       * because the start of the deque map is initialized only lazily and
       * could easily match something based on those stale starting bytes.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        return (allocation.Size() < 2 * sizeof(Offset));
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        if (!CheckDequeMapAnchorIn(reader, index, allocation,
                                   _graph.GetStaticAnchors(index))) {
          CheckDequeMapAnchorIn(reader, index, allocation,
                                _graph.GetStackAnchors(index));
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

  bool CheckDequeMapAnchorIn(Reader& reader, AllocationIndex index,
                             const Allocation& allocation,
                             const std::vector<Offset>* anchors) {
    Offset address = allocation.Address();
    if (anchors != nullptr) {
      typename VirtualAddressMap<Offset>::Reader dequeReader(_addressMap);
      for (Offset anchor : *anchors) {
        if (_anchorIterator == _endIterator ||
            anchor < _anchorIterator.Base() ||
            anchor + sizeof(Offset) > _anchorIterator.Limit()) {
          /*
           * We know the following call will find something because
           * the address was read to determine that it was a anchor.
           */
          _anchorIterator = _addressMap.find(anchor);
        }
        const char* image = _anchorIterator.GetImage();
        Offset base = _anchorIterator.Base();
        Offset limit = _anchorIterator.Limit();

        if (anchor + sizeof(Offset) > limit) {
          continue;
        }
        Offset* asOffsets = (Offset*)(image + (anchor - base));
        if (asOffsets[0] != address) {
          /*
           * For any of the anchor points we might match (buckets, first block
           * or last block) we require a pointer to the start of the
           * allocation.
           */
          continue;
        }

        if (anchor + NUM_OFFSETS_IN_HEADER * sizeof(Offset) <= limit) {
          /*
           * We have enough contiguous space from the start of the anchor
           * that it could be the start of a deque, in which case the
           * anchor point allocation would be a map.
           */
          if (TagAllocationsIfDeque(_numAllocations, asOffsets, reader, index,
                                    allocation)) {
            return true;
          }
        }

        /*
         * A deque that is on the stack or static also has at least
         * one anchor for the block associated with the start and possibly
         * another for the finish.  Note that unlike in the case of embedded
         * references, which we check in increasing address order, we have to
         * check for a possibility of a start or finish block as an anchor
         * point because otherwise a weaker allocation checker, even though
         * it runs at a later phase on each allocation, can have an
         * opportunity to tag the start or finish block wrongly as long as
         * the address of the start or finish block is less than the address
         * of the buckets.  One pattern that could otherwise mis-tag
         * deque blocks is %VectorBody.
         */
        if ((anchor + 3 * sizeof(Offset) > limit) ||
            (anchor - 3 * sizeof(Offset) < base)) {
          /*
           * If we don't have at least this much range for part of the deque
           * we don't have any chance that this anchor would be for the
           * start or end block.
           */
          continue;
        }
        if (asOffsets[-1] < address || asOffsets[-1] > asOffsets[1] ||
            address >= asOffsets[1]) {
          continue;
        }
        Offset mNode = asOffsets[2];
        if (_mapReader.ReadOffset(mNode, 0xbad) != address) {
          continue;
        }
        AllocationIndex mapIndex = _directory.AllocationIndexOf(mNode);
        if (mapIndex == _numAllocations) {
          continue;
        }
        const Allocation* mapAllocation = _directory.AllocationAt(mapIndex);

        Offset bucketsAddress = mapAllocation->Address();
        if (asOffsets[-3] == bucketsAddress) {
          /*
           * It could only be the first block at this point.
           */
          if (anchor + 7 * sizeof(Offset) > limit) {
            continue;
          }
          if (TagAllocationsIfDeque(_numAllocations, asOffsets - 3, _mapReader,
                                    mapIndex, *mapAllocation)) {
            return true;
          }
        } else if (anchor - 7 * sizeof(Offset) >= base) {
          if (asOffsets[-7] == bucketsAddress) {
            if (TagAllocationsIfDeque(_numAllocations, asOffsets - 7,
                                      _mapReader, mapIndex, *mapAllocation)) {
              return true;
            }
          }
        }
      }
    }
    return false;
  }

  bool TagAllocationsIfDeque(AllocationIndex dequeHolderIndex,
                             const Offset* dequeImage, Reader& mapReader,
                             AllocationIndex mapIndex,
                             const Allocation& mapAllocation) {
    Offset mapAddress = dequeImage[0];
    /*
     * If we have a specific address in mind for the map, it must match
     * the address at the start of the deque.
     */
    if (mapAddress != mapAllocation.Address()) {
      return false;
    }
    Offset maxEntries = dequeImage[1];
    if (maxEntries == 0) {
      return false;
    }

    Offset liveAreaLimit = mapAddress + maxEntries * sizeof(Offset);
    Offset startMNode = dequeImage[5];
    if ((startMNode & (sizeof(Offset) - 1)) != 0 || startMNode < mapAddress ||
        startMNode >= liveAreaLimit) {
      return false;
    }
    Offset finishMNode = dequeImage[9];
    if (finishMNode != startMNode &&
        ((finishMNode & (sizeof(Offset) - 1)) != 0 ||
         finishMNode < mapAddress || finishMNode >= liveAreaLimit)) {
      return false;
    }

    Offset startCur = dequeImage[2];
    if (startCur == 0xbad) {
      return false;
    }
    Offset startFirst = dequeImage[3];
    if (startFirst == 0xbad || startCur < startFirst) {
      return false;
    }
    Offset startLast = dequeImage[4];
    if (startLast == 0xbad || startCur >= startLast) {
      return false;
    }
    Offset finishCur = dequeImage[6];
    Offset finishFirst = dequeImage[7];
    Offset finishLast = dequeImage[8];
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

    Offset maxMaxEntries = (mapAllocation.Size()) / sizeof(Offset);

    /*
     * Warning: For very large allocations, where malloc is asked for an exact
     * multiple of pages, malloc must given an extra page to compensate for the
     * need to store the size/status value, so the size will be 0xff8 or 0xffc
     * larger than expected, given a 64-bit process or 32-bit process,
     * respectively.  Given that we check the block pointers anyways leave
     * the check for a minimum maxEntries (really _M_map_size) somwhat
     * relaxed.
     */
    Offset minMaxEntries = _directory.MinRequestSize(mapIndex) / sizeof(Offset);

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
      AllocationIndex blockIndex =
          _graph.TargetAllocationIndex(mapIndex, blockAddress);
      if (blockIndex == _numAllocations) {
        return false;
      }
      const Allocation* blockAllocation = _directory.AllocationAt(blockIndex);
      if (blockAllocation == nullptr) {
        return false;
      }
      if (blockAllocation->Address() != blockAddress) {
        return false;
      }
    }
    typename VirtualAddressMap<Offset>::Reader blockReader(_addressMap);
    _tagHolder.TagAllocation(mapIndex, _mapTagIndex);
    _edgeIsTainted.SetAllOutgoing(mapIndex, true);

    /*
     * The only incoming reference to a deque map that is considered favored
     * is from the allocation, if any, that holds the deque.
     */

    if (dequeHolderIndex != _numAllocations) {
      _edgeIsFavored.Set(dequeHolderIndex, mapIndex, true);
    }
    Offset blockSize = startLast - startFirst;
    for (Offset mNode = startMNode; mNode <= finishMNode;
         mNode += sizeof(Offset)) {
      Offset blockAddr = mapReader.ReadOffset(mNode, 0);
      AllocationIndex blockIndex =
          _graph.TargetAllocationIndex(mapIndex, blockAddr);
      _edgeIsTainted.Set(mapIndex, blockIndex, false);
      _tagHolder.TagAllocation(blockIndex, _blockTagIndex);

      /*
       * The live reference from the deque map to the deque block
       * is considered favored.
       */

      _edgeIsFavored.Set(mapIndex, blockIndex, true);

      /*
       * Outgoing references from each deque block are considered tainted
       * unless they are in the live part of the deque block.
       */

      _edgeIsTainted.SetAllOutgoing(blockIndex, true);
      Offset liveStart = (mNode == startMNode) ? startCur : blockAddr;
      Offset liveLimit =
          ((mNode == finishMNode) ? finishCur : (blockAddr + blockSize)) &
          ~(sizeof(Offset) - 1);
      for (Offset liveAddr = liveStart; liveAddr < liveLimit;
           liveAddr += sizeof(Offset)) {
        Offset targetAddr = blockReader.ReadOffset(liveAddr, 0);
        AllocationIndex targetIndex =
            _graph.TargetAllocationIndex(blockIndex, targetAddr);
        if (targetIndex != _numAllocations) {
          _edgeIsTainted.Set(blockIndex, targetIndex, false);
        }
      }
    }
    return true;
  }

  /*
   * Check whether the specified allocation contains any deques.  If so,
   * tag the associated deque maps and any associated deque blocks.
   */
  bool TagFromContainedDeques(AllocationIndex index,
                              const ContiguousImage& contiguousImage,
                              Phase phase, const Allocation& allocation,
                              const AllocationIndex* unresolvedOutgoing) {
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        return allocation.Size() < NUM_OFFSETS_IN_HEADER * sizeof(Offset);
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckEmbeddedDeques(index, contiguousImage, unresolvedOutgoing);
        break;
      case Tagger::WEAK_CHECK:
        // May be expensive, weak results OK
        // An example here might be if one of the nodes in the chain is no
        // longer allocated.
        break;
    }
    return false;
  }

  void CheckEmbeddedDeques(AllocationIndex index,
                           const ContiguousImage& contiguousImage,
                           const AllocationIndex* unresolvedOutgoing) {
    Reader mapReader(_addressMap);

    const Offset* offsetLimit =
        contiguousImage.OffsetLimit() - (NUM_OFFSETS_IN_HEADER - 1);
    const Offset* firstOffset = contiguousImage.FirstOffset();

    for (const Offset* check = firstOffset; check < offsetLimit; check++) {
      AllocationIndex mapIndex = unresolvedOutgoing[check - firstOffset];
      if (mapIndex == _numAllocations) {
        continue;
      }
      if (_tagHolder.IsStronglyTagged(mapIndex)) {
        // Don't override any strongly tagged allocations.
        continue;
      }

      if (TagAllocationsIfDeque(index, check, mapReader, mapIndex,
                                *(_directory.AllocationAt(mapIndex)))) {
        check += (NUM_OFFSETS_IN_HEADER - 1);
      }
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
