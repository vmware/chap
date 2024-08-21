// Copyright (c) 2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/EdgePredicate.h"
#include "../Allocations/Graph.h"
#include "../Allocations/TagHolder.h"
#include "../Allocations/Tagger.h"
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace GoLang {
template <typename Offset>
class AllocationsTagger : public Allocations::Tagger<Offset> {
 public:
  typedef typename Allocations::Graph<Offset>::EdgeIndex EdgeIndex;
  typedef typename Allocations::Directory<Offset> Directory;
  typedef typename Allocations::Tagger<Offset> Tagger;
  typedef typename Tagger::Phase Phase;
  typedef typename Directory::AllocationIndex AllocationIndex;
  typedef typename Directory::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename TagHolder::TagIndex TagIndex;
  typedef typename Allocations::EdgePredicate<Offset> EdgePredicate;
  AllocationsTagger(const Allocations::Graph<Offset>& graph,
                    TagHolder& tagHolder, EdgePredicate& edgeIsTainted,
                    EdgePredicate& edgeIsFavored,
                    const InfrastructureFinder<Offset>& infrastructureFinder,
                    size_t mappedPageRangeAllocationFinderIndex,
                    const VirtualAddressMap<Offset>& virtualAddressMap)
      : _graph(graph),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _tagHolder(tagHolder),
        _edgeIsTainted(edgeIsTainted),
        _edgeIsFavored(edgeIsFavored),
        _infrastructureFinder(infrastructureFinder),
        _mappedPageRangeAllocationFinderIndex(
            mappedPageRangeAllocationFinderIndex),
        _virtualAddressMap(virtualAddressMap),
        _reader(_virtualAddressMap),
        _goRoutineTagIndex(_tagHolder.RegisterTag("%GoRoutine", true, false)),
        _goRoutineStackTagIndex(
            _tagHolder.RegisterTag("%GoRoutineStack", true, true)),
        _goChannelTagIndex(_tagHolder.RegisterTag("%GoChannel", true, false)),
        _goChannelBufferTagIndex(
            _tagHolder.RegisterTag("%GoChannelBuffer", true, true)),
        _enabled(_infrastructureFinder.GetArenasFieldValue() != 0) {}

  bool TagFromAllocation(const ContiguousImage& contiguousImage, Reader& reader,
                         AllocationIndex index, Phase phase,
                         const Allocation& allocation, bool isUnsigned) {
    if (!_enabled) {
      return true;  // There is nothing more to check.
    }
    if (_tagHolder.IsStronglyTagged(index)) {
      // This allocation was already strongly tagged as something else.
      return true;  // We are finished looking at this allocation.
    }

    if (allocation.FinderIndex() != _mappedPageRangeAllocationFinderIndex) {
      // The tagged GoLang allocations are only from this particular finder
      // so there is no further processing needed by this tagger for the
      // given allocation.
      return true;
    }

    if (!isUnsigned) {
      return true;
    }

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        if (!TagAsGoRoutine(reader, index, allocation)) {
          TagAsGoChannel(reader, index, allocation);
        }
        return true;
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        break;
      case Tagger::WEAK_CHECK:
        // May be expensive, weak results OK
        // An example here might be if one of the nodes in the chain is
        // no
        // longer allocated.
        break;
    }
    return false;
  }

 private:
  const Allocations::Graph<Offset>& _graph;
  const Allocations::Directory<Offset>& _directory;
  const AllocationIndex _numAllocations;
  TagHolder& _tagHolder;
  EdgePredicate& _edgeIsTainted;
  EdgePredicate& _edgeIsFavored;
  const InfrastructureFinder<Offset> _infrastructureFinder;
  size_t _mappedPageRangeAllocationFinderIndex;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  Reader _reader;
  TagIndex _goRoutineTagIndex;
  TagIndex _goRoutineStackTagIndex;
  TagIndex _goChannelTagIndex;
  TagIndex _goChannelBufferTagIndex;
  bool _enabled;

  bool TagAsGoRoutine(Reader& reader, AllocationIndex index,
                      const Allocation& allocation) {
    Offset allocationAddress = allocation.Address();
    if (allocation.Size() < 0x180) {
      return false;
    }
    if (!_infrastructureFinder.IsPlausibleGoRoutine(reader,
                                                    allocationAddress)) {
      return false;
    }
    _tagHolder.TagAllocation(index, _goRoutineTagIndex);
    Offset stack = reader.ReadOffset(allocationAddress, 0);
    if (stack != 0) {
      AllocationIndex stackIndex = _graph.TargetAllocationIndex(index, stack);
      if (stackIndex != _numAllocations) {
        _tagHolder.TagAllocation(stackIndex, _goRoutineStackTagIndex);
        _edgeIsFavored.Set(index, stackIndex, true);
      }
    }
    return true;
  }
  bool TagAsGoChannel(Reader& reader, AllocationIndex index,
                      const Allocation& allocation) {
    Offset allocationAddress = allocation.Address();
    Offset allocationSize = allocation.Size();
    if (allocationSize != 0x60) {
      return false;
    }
    Offset addressOfBufField = allocationAddress + 2 * sizeof(Offset);
    Offset buffer = reader.ReadOffset(addressOfBufField, 0);
    if ((buffer == 0) || ((buffer & (sizeof(Offset) - 1)) != 0)) {
      return false;
    }
    Offset abiType =
        reader.ReadOffset(allocationAddress + 4 * sizeof(Offset), 0);
    if ((abiType == 0) || ((abiType & (sizeof(Offset) - 1)) != 0)) {
      return false;
    }

    auto it = _virtualAddressMap.find(abiType);
    if (it == _virtualAddressMap.end()) {
      return false;
    }
    if ((it.Flags() &
         (RangeAttributes::IS_WRITABLE | RangeAttributes::IS_READABLE |
          RangeAttributes::IS_EXECUTABLE)) != RangeAttributes::IS_READABLE) {
      return false;
    }

    Offset allocationLimit = allocationAddress + allocationSize;
    if (buffer >= allocationAddress && buffer < allocationLimit) {
      if (buffer != addressOfBufField && buffer != (allocationAddress + 0x58) &&
          buffer != allocationAddress + 0x60) {
        return false;
      }

      _tagHolder.TagAllocation(index, _goChannelTagIndex);
      return true;
    }
    AllocationIndex bufferIndex = _graph.TargetAllocationIndex(index, buffer);
    if (bufferIndex == _numAllocations) {
      return false;
    }
    _tagHolder.TagAllocation(index, _goChannelTagIndex);
    _tagHolder.TagAllocation(bufferIndex, _goChannelBufferTagIndex);
    _edgeIsFavored.Set(index, bufferIndex, true);
    return true;
  }
};
}  // namespace GoLang
}  // namespace chap
