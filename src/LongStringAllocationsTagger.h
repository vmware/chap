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
class LongStringAllocationsTagger : public Allocations::Tagger<Offset> {
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
  LongStringAllocationsTagger(Graph& graph, TagHolder& tagHolder)
      : _graph(graph),
        _tagHolder(tagHolder),
        _finder(graph.GetAllocationFinder()),
        _numAllocations(_finder.NumAllocations()),
        _addressMap(_finder.GetAddressMap()),
        _charsTagIndex(_tagHolder.RegisterTag("long string chars")) {}

  bool TagFromAllocation(Reader& reader, Pass pass, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    switch (pass) {
      case Tagger::FIRST_PASS_THROUGH_ALLOCATIONS:
        return TagAnchorPointLongStringChars(index, phase, allocation);
        break;
      case Tagger::LAST_PASS_THROUGH_ALLOCATIONS:
        return TagFromContainedStrings(reader, index, phase, allocation);
        break;
    }
    /*
     * There is no need to look any more at this allocation during this pass.
     */
    return true;
  }

  TagIndex GetCharsTagIndex() const { return _charsTagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  const Finder& _finder;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  TagIndex _charsTagIndex;

  /*
   * Check whether the specified allocation holds a long string, for the current
   * style of strings without COW string bodies, where the std::string is
   * on the stack or statically allocated, tagging it if so.
   * Return true if no further work is needed to check.
   */
  bool TagAnchorPointLongStringChars(AllocationIndex index, Phase phase,
                                     const Allocation& allocation) {
    if (_tagHolder.GetTagIndex(index) != 0) {
      /*
       * This was already tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation does not hold the
       * characters for a long string.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }
    Offset size = allocation.Size();

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        if (size < 2 * sizeof(Offset)) {
          return true;
        }
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        if (size < 10 * sizeof(Offset)) {
          TagIfLongStringCharsAnchorPoint(index, allocation);
          return true;
        }
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        TagIfLongStringCharsAnchorPoint(index, allocation);
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

  void TagIfLongStringCharsAnchorPoint(AllocationIndex index,
                                       const Allocation& allocation) {
    Offset stringLength = 0;
    const char* allocationImage;
    Offset address = allocation.Address();
    Offset size = allocation.Size();
    Offset numBytesFound =
        _addressMap.FindMappedMemoryImage(address, &allocationImage);

    if (numBytesFound < size) {
      return;
    }
    while (allocationImage[stringLength] != '\000') {
      if (++stringLength == size) {
        return;
      }
    }
    if (stringLength < 2 * sizeof(Offset)) {
      return;
    }
    if (!CheckLongStringAnchorIn(index, allocation, stringLength,
                                 _graph.GetStaticAnchors(index))) {
      CheckLongStringAnchorIn(index, allocation, stringLength,
                              _graph.GetStackAnchors(index));
    }
  }
  bool CheckLongStringAnchorIn(AllocationIndex charsIndex,
                               const Allocation& charsAllocation,
                               Offset stringLength,
                               const std::vector<Offset>* anchors) {
    Offset charsAddress = charsAllocation.Address();
    Offset charsSize = charsAllocation.Size();
    if (anchors != nullptr) {
      typename VirtualAddressMap<Offset>::Reader stringReader(_addressMap);
      for (Offset anchor : *anchors) {
        if (stringReader.ReadOffset(anchor, 0xbad) != charsAddress) {
          continue;
        }
        if (stringReader.ReadOffset(anchor + sizeof(Offset), 0) !=
            stringLength) {
          continue;
        }
        Offset capacity =
            stringReader.ReadOffset(anchor + 2 * sizeof(Offset), 0);
        if ((capacity < stringLength) || (capacity > charsSize) ||
            (3 * capacity < 2 * charsSize)) {
          continue;
        }

        _tagHolder.TagAllocation(charsIndex, _charsTagIndex);
        return true;
      }
    }
    return false;
  }

  /*
   * Check whether the specified allocation contains any strings (but not the
   * old style that uses COW string bodies).  If so, for any of those strings
   * that are sufficiently long to use external buffers, tag the external
   * buffers.
   */
  bool TagFromContainedStrings(Reader& reader, AllocationIndex /*index*/,
                               Phase phase, const Allocation& allocation) {
    /*
     * We don't care about the index, because we aren't tagging the allocation
     * that contains the strings but only any external buffers used for long
     * strings.
     */
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        return allocation.Size() < 4 * sizeof(Offset);
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckEmbeddedStrings(reader, allocation.Address(), allocation.Size());
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

  void CheckEmbeddedStrings(Reader& stringReader, Offset address, Offset size) {
    Reader charsReader(_addressMap);
    Offset checkLimit =
        address + (size & ~(sizeof(Offset) - 1)) - 3 * sizeof(Offset);
    for (Offset checkAt = address; checkAt < checkLimit;
         checkAt += sizeof(Offset)) {
      Offset charsAddress = stringReader.ReadOffset(checkAt, 0);
      if (charsAddress == 0) {
        continue;
      }

      Offset stringLength =
          stringReader.ReadOffset(checkAt + sizeof(Offset), 0);
      if (stringLength < 2 * sizeof(Offset)) {
        continue;
      }

      Offset capacity =
          stringReader.ReadOffset(checkAt + 2 * sizeof(Offset), 0);
      if (stringLength > capacity) {
        continue;
      }

      AllocationIndex charsIndex = _finder.AllocationIndexOf(charsAddress);
      if (charsIndex == _numAllocations) {
        continue;
      }
      const Allocation* charsAllocation = _finder.AllocationAt(charsIndex);
      if (charsAllocation->Address() != charsAddress) {
        continue;
      }
      Offset charsSize = charsAllocation->Size();

      if (capacity >= charsSize) {
        continue;
      }

      if (3 * capacity < 2 * charsSize) {
        continue;
      }

      const char* allocationImage;
      Offset numBytesFound =
          _addressMap.FindMappedMemoryImage(charsAddress, &allocationImage);

      if (numBytesFound < charsSize) {
        continue;
      }

      if (allocationImage[stringLength] != '\000') {
        continue;
      }
      if (stringLength == (Offset)(strlen(allocationImage))) {
        _tagHolder.TagAllocation(charsIndex, _charsTagIndex);
        checkAt += 3 * sizeof(Offset);
      }
    }
  }
};
}  // namespace chap
