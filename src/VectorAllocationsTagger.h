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
class VectorAllocationsTagger : public Allocations::Tagger<Offset> {
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
  VectorAllocationsTagger(Graph& graph, TagHolder& tagHolder)
      : _graph(graph),
        _tagHolder(tagHolder),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _addressMap(graph.GetAddressMap()),
        _tagIndex(_tagHolder.RegisterTag("%VectorBody")) {}

  bool TagFromAllocation(const ContiguousImage& /* contiguousImage */,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    /*
     * Note that we cannot assume anything based on the start of a vector
     * body because we don't know the type of the entries.  For this reason
     * we ignore whether the allocation is signed.
     */

    if (_tagHolder.GetTagIndex(index) != 0) {
      /*
       * This was already tagged as something other than a vector body.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        return (allocation.Size() < 2 * sizeof(Offset));
        break;
      case Tagger::MEDIUM_CHECK:
        break;
      case Tagger::SLOW_CHECK:
        break;
      case Tagger::WEAK_CHECK:
        /*
         * Recognition of a vector body is rather weak because
         * we don't know much about the body itself and so depend on finding
         * the corresponding vector as a way of finding each vector body. A
         * challenge here is part of a deque can look like a vector body.
         * Rather than build in knowledge of these other possible matches
         * let those more reliable patterns run first during the non-weak
         * phase on the corresponding allocation.
         */

        if (!CheckVectorBodyAnchorIn(index, allocation,
                                     _graph.GetStaticAnchors(index))) {
          CheckVectorBodyAnchorIn(index, allocation,
                                  _graph.GetStackAnchors(index));
        }
        return true;
        break;
    }
    return false;
  }

  bool TagFromReferenced(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex /* index */,
                         Phase phase, const Allocation& allocation,
                         const AllocationIndex* unresolvedOutgoing) {
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        return allocation.Size() < 3 * sizeof(Offset);
        break;
      case Tagger::MEDIUM_CHECK:
        break;
      case Tagger::SLOW_CHECK:
        break;
      case Tagger::WEAK_CHECK:
        /*
         * Recognition of a vector body is rather weak because
         * we don't know much about the body itself and so depend on finding
         * the corresponding vector as a way of finding each vector body. A
         * challenge here is part of a deque can look like a vector body.
         * Rather than build in knowledge of these other possible matches
         * let those more reliable patterns run first during the non-weak
         * phase on the corresponding allocation.
         */

        CheckEmbeddedVectors(contiguousImage, unresolvedOutgoing);
        break;
    }
    return false;
  }

  TagIndex GetTagIndex() const { return _tagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  const Directory& _directory;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  TagIndex _tagIndex;

  bool CheckVectorBodyAnchorIn(AllocationIndex bodyIndex,
                               const Allocation& bodyAllocation,
                               const std::vector<Offset>* anchors) {
    Offset bodyAddress = bodyAllocation.Address();
    Offset bodyLimit = bodyAddress + bodyAllocation.Size();
    Offset minCapacity = _directory.MinRequestSize(bodyIndex);
    if (minCapacity < 1) {
      minCapacity = 1;
    }
    if (anchors != nullptr) {
      typename VirtualAddressMap<Offset>::Reader dequeReader(_addressMap);
      for (Offset anchor : *anchors) {
        const char* image;
        Offset numBytesFound =
            _addressMap.FindMappedMemoryImage(anchor, &image);

        if (numBytesFound < 3 * sizeof(Offset)) {
          continue;
        }
        Offset* check = (Offset*)(image);
        if (check[0] != bodyAddress) {
          continue;
        }
        Offset useLimit = check[1];
        if (useLimit < bodyAddress) {
          continue;
        }

        Offset capacityLimit = check[2];
        if (capacityLimit < useLimit || capacityLimit > bodyLimit ||
            (capacityLimit - bodyAddress) < minCapacity) {
          continue;
        }

        _tagHolder.TagAllocation(bodyIndex, _tagIndex);

        return true;
      }
    }
    return false;
  }

  void CheckEmbeddedVectors(const ContiguousImage& contiguousImage,
                            const AllocationIndex* unresolvedOutgoing) {
    Reader mapReader(_addressMap);

    const Offset* offsetLimit = contiguousImage.OffsetLimit() - 2;
    const Offset* firstOffset = contiguousImage.FirstOffset();

    for (const Offset* check = firstOffset; check < offsetLimit; check++) {
      AllocationIndex bodyIndex = unresolvedOutgoing[check - firstOffset];
      if (bodyIndex == _numAllocations) {
        continue;
      }
      if (_tagHolder.GetTagIndex(bodyIndex) != 0) {
        continue;
      }
      const Allocation* allocation = _directory.AllocationAt(bodyIndex);

      Offset address = allocation->Address();
      Offset bodyLimit = address + allocation->Size();
      if (check[0] != address) {
        continue;
      }

      Offset useLimit = check[1];
      if (useLimit < address) {
        continue;
      }

      Offset capacityLimit = check[2];
      if (capacityLimit < useLimit || capacityLimit > bodyLimit ||
          capacityLimit == address ||
          (capacityLimit - address) < _directory.MinRequestSize(bodyIndex)) {
        continue;
      }

      /*
       * Warning: If the variant of malloc has nothing like a size/status
       * word between the allocations we will have trouble parsing
       * BLLl where L is the limit of one allocation and l is the limit of
       * the next, because this could be a full vector body starting at B
       * or an empty vector body starting at L.  Fortunately, with libc
       * malloc we do not yet have this problem.
       */
      _tagHolder.TagAllocation(bodyIndex, _tagIndex);
      check += 2;
    }
  }
};
}  // namespace chap
