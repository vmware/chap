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
  typedef typename Allocations::EdgePredicate<Offset> EdgePredicate;
  typedef typename TagHolder::TagIndex TagIndex;
  static constexpr int NUM_OFFSETS_IN_HEADER = 3;
  VectorAllocationsTagger(
      Graph& graph, TagHolder& tagHolder, EdgePredicate& edgeIsTainted,
      EdgePredicate& edgeIsFavored,
      const Allocations::SignatureDirectory<Offset>& signatureDirectory)
      : _graph(graph),
        _tagHolder(tagHolder),
        _edgeIsTainted(edgeIsTainted),
        _edgeIsFavored(edgeIsFavored),
        _signatureDirectory(signatureDirectory),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _addressMap(graph.GetAddressMap()),
        _tagIndex(_tagHolder.RegisterTag("%VectorBody", false, true)) {}

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool isUnsigned) {
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        if (_tagHolder.GetTagIndex(index) != 0) {
          /*
           * This was already tagged as something other than a vector body.
           */
          return true;
        }
        if (!isUnsigned) {
          /*
           * Strictly speaking, we can't assume that something that is signed is
           * not a vector body, because we don't know the type of the
           * individual elements and, for example, it could be a vector body for
           * a vector<T>, where objects of type T have vtable pointers at the
           * start, or it could be a vector body for a vector<const char *>,
           * where those pointers are to read-only memory.  At present, simply
           * because it is much more common to have a typed object classified
           * falsely as a vector than it is to have vectors containing objects
           * that have vtable pointers, we'll choose to err by not matching the
           * pattern in the case a vtable pointer is present.
           */
          const Offset* offsetLimit = contiguousImage.OffsetLimit();
          const Offset* firstOffset = contiguousImage.FirstOffset();
          if (offsetLimit > firstOffset) {
            if (_signatureDirectory.IsKnownVtablePointer(*firstOffset)) {
              return true;
            }
          }
        }
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

        if (_tagHolder.GetTagIndex(index) != 0) {
          /*
           * This was already tagged as something other than a vector body.
           */
          return true;
        }
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
                         Reader& /* reader */, AllocationIndex index,
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

        CheckEmbeddedVectors(index, contiguousImage, unresolvedOutgoing);
        break;
    }
    return false;
  }

  TagIndex GetTagIndex() const { return _tagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  EdgePredicate& _edgeIsTainted;
  EdgePredicate& _edgeIsFavored;
  const Allocations::SignatureDirectory<Offset>& _signatureDirectory;
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
        MarkTaintedOutgoingEdges(bodyIndex, bodyAddress, useLimit);

        return true;
      }
    }
    return false;
  }

  void MarkTaintedOutgoingEdges(AllocationIndex bodyIndex, Offset bodyAddress,
                                Offset useLimit) {
    _edgeIsTainted.SetAllOutgoing(bodyIndex, true);
    useLimit = useLimit & ~(sizeof(Offset) - 1);
    Reader reader(_addressMap);
    for (Offset addrInBody = bodyAddress; addrInBody < useLimit;
         addrInBody += sizeof(Offset)) {
      Offset candidate = reader.ReadOffset(addrInBody, 0);
      if (candidate == 0) {
        continue;
      }
      AllocationIndex target =
          _graph.TargetAllocationIndex(bodyIndex, candidate);
      if (target != _numAllocations) {
        _edgeIsTainted.Set(bodyIndex, target, false);
      }
    }
  }

  void CheckEmbeddedVectors(const AllocationIndex index,
                            const ContiguousImage& contiguousImage,
                            const AllocationIndex* unresolvedOutgoing) {
    Reader bodyReader(_addressMap);

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

      if (bodyLimit - address >= sizeof(Offset)) {
        /*
         * For cases where an allocation looks both like it starts with a
         * vtable pointer and where it appears to be referenced like a
         * vector, treat it as being of the type correspoinding to the
         * allocation.  This may result in missing tagging a few vector
         * bodies but likely eliminates more false tagging due to stale
         * references.
         */
        if (_signatureDirectory.IsKnownVtablePointer(
                bodyReader.ReadOffset(address, 0xbad))) {
          continue;
        }
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
      MarkTaintedOutgoingEdges(bodyIndex, address, useLimit);
      _edgeIsFavored.Set(index, bodyIndex, true);
      check += (NUM_OFFSETS_IN_HEADER - 1);
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
