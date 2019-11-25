// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/Graph.h"
#include "Allocations/TagHolder.h"
#include "Allocations/Tagger.h"
#include "ModuleDirectory.h"
#include "VirtualAddressMap.h"

namespace chap {
template <typename Offset>
class LongStringAllocationsTagger : public Allocations::Tagger<Offset> {
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
  LongStringAllocationsTagger(Graph& graph, TagHolder& tagHolder,
                              const ModuleDirectory<Offset> moduleDirectory)
      : _graph(graph),
        _tagHolder(tagHolder),
        _finder(graph.GetAllocationFinder()),
        _numAllocations(_finder.NumAllocations()),
        _addressMap(_finder.GetAddressMap()),
        _charsImage(_finder),
        _staticAnchorReader(_addressMap),
        _stackAnchorReader(_addressMap),
        _enabled(true),
        _charsTagIndex(_tagHolder.RegisterTag("long string chars")) {
    bool foundCheckableLibrary = false;
    for (typename ModuleDirectory<Offset>::const_iterator it =
             moduleDirectory.begin();
         it != moduleDirectory.end(); ++it) {
      if (it->first.find("libstdc++.so.6") != std::string::npos) {
        foundCheckableLibrary = true;
      }
    }

    if (!foundCheckableLibrary) {
      return;
    }

    for (typename ModuleDirectory<Offset>::const_iterator it =
             moduleDirectory.begin();
         it != moduleDirectory.end(); ++it) {
      typename ModuleDirectory<Offset>::RangeToFlags::const_iterator itRange =
          it->second.begin();
      const auto& itRangeEnd = it->second.end();

      for (; itRange != itRangeEnd; ++itRange) {
        if ((itRange->_value &
             ~VirtualAddressMap<Offset>::RangeAttributes::IS_EXECUTABLE) ==
            (VirtualAddressMap<Offset>::RangeAttributes::IS_READABLE |
             VirtualAddressMap<Offset>::RangeAttributes::HAS_KNOWN_PERMISSIONS |
             VirtualAddressMap<Offset>::RangeAttributes::IS_MAPPED)) {
          Offset base = itRange->_base;
          Offset limit = itRange->_limit;
          typename VirtualAddressMap<Offset>::const_iterator itVirt =
              _addressMap.find(base);
          const char* check = itVirt.GetImage() + (base - itVirt.Base());
          const char* checkLimit = check + (limit - base) - 26;
          for (; check < checkLimit; check++) {
            if (!strncmp(check, "_ZNSt7__cxx1112basic_string", 27)) {
              _enabled = true;
              return;
            }
          }
        }
      }
    }
    _enabled = false;
  }

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    if (!_enabled) {
      // The C++11 ABI doesn't appear to have been used in the process.
      return true;
    }
    if (_tagHolder.GetTagIndex(index) != 0) {
      /*
       * This was already tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation does not hold the
       * characters for a long string.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }
    return TagAnchorPointLongStringChars(contiguousImage, index, phase,
                                         allocation);
  }

  bool TagFromReferenced(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex /* index */,
                         Phase phase, const Allocation& allocation,
                         const AllocationIndex* unresolvedOutgoing) {
    if (!_enabled) {
      // The C++11 ABI doesn't appear to have been used in the process.
      return true;
    }
    return TagFromContainedStrings(contiguousImage, phase, allocation,
                                   unresolvedOutgoing);
  }

  TagIndex GetCharsTagIndex() const { return _charsTagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  const Finder& _finder;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  ContiguousImage _charsImage;
  typename VirtualAddressMap<Offset>::Reader _staticAnchorReader;
  typename VirtualAddressMap<Offset>::Reader _stackAnchorReader;
  bool _enabled;
  TagIndex _charsTagIndex;

  /*
   * Check whether the specified allocation holds a long string, for the current
   * style of strings without COW string bodies, where the std::string is
   * on the stack or statically allocated, tagging it if so.
   * Return true if no further work is needed to check.
   */
  bool TagAnchorPointLongStringChars(const ContiguousImage& contiguousImage,
                                     AllocationIndex index, Phase phase,
                                     const Allocation& allocation) {
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
          TagIfLongStringCharsAnchorPoint(contiguousImage, index, allocation);
          return true;
        }
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        TagIfLongStringCharsAnchorPoint(contiguousImage, index, allocation);
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

  void TagIfLongStringCharsAnchorPoint(const ContiguousImage& contiguousImage,
                                       AllocationIndex index,
                                       const Allocation& allocation) {
    Offset size = allocation.Size();
    Offset stringLength = strnlen(contiguousImage.FirstChar(), size);
    if (stringLength == size) {
      return;
    }
    if (stringLength < 2 * sizeof(Offset)) {
      return;
    }
    if (!CheckLongStringAnchorIn(index, allocation, stringLength,
                                 _graph.GetStaticAnchors(index),
                                 _staticAnchorReader)) {
      CheckLongStringAnchorIn(index, allocation, stringLength,
                              _graph.GetStackAnchors(index),
                              _stackAnchorReader);
    }
  }
  bool CheckLongStringAnchorIn(AllocationIndex charsIndex,
                               const Allocation& charsAllocation,
                               Offset stringLength,
                               const std::vector<Offset>* anchors,
                               Reader& anchorReader) {
    Offset charsAddress = charsAllocation.Address();
    Offset charsSize = charsAllocation.Size();
    if (anchors != nullptr) {
      for (Offset anchor : *anchors) {
        if (anchorReader.ReadOffset(anchor, 0xbad) != charsAddress) {
          continue;
        }
        if (anchorReader.ReadOffset(anchor + sizeof(Offset), 0) !=
            stringLength) {
          continue;
        }
        Offset capacity =
            anchorReader.ReadOffset(anchor + 2 * sizeof(Offset), 0);
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
  bool TagFromContainedStrings(const ContiguousImage& contiguousImage,
                               Phase phase, const Allocation& allocation,
                               const AllocationIndex* unresolvedOutgoing) {
    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        return allocation.Size() < 4 * sizeof(Offset);
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        CheckEmbeddedStrings(contiguousImage, unresolvedOutgoing);
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

  void CheckEmbeddedStrings(const ContiguousImage& contiguousImage,
                            const AllocationIndex* unresolvedOutgoing) {
    Reader charsReader(_addressMap);
    const Offset* checkLimit = contiguousImage.OffsetLimit() - 3;
    const Offset* firstCheck = contiguousImage.FirstOffset();
    ;
    for (const Offset* check = firstCheck; check < checkLimit; check++) {
      AllocationIndex charsIndex = unresolvedOutgoing[check - firstCheck];
      if (charsIndex == _numAllocations) {
        continue;
      }
      if (_tagHolder.GetTagIndex(charsIndex) != 0) {
        continue;
      }
      Offset charsAddress = check[0];
      const Allocation* charsAllocation = _finder.AllocationAt(charsIndex);
      if (charsAllocation->Address() != charsAddress) {
        continue;
      }

      Offset stringLength = check[1];
      if (stringLength < 2 * sizeof(Offset)) {
        continue;
      }

      Offset capacity = check[2];
      if (stringLength > capacity) {
        continue;
      }

      Offset charsSize = charsAllocation->Size();

      if (capacity >= charsSize) {
        continue;
      }

      if (3 * capacity < 2 * charsSize) {
        continue;
      }

      _charsImage.SetIndex(charsIndex);
      const char* pFirstChar = _charsImage.FirstChar();

      if (*(pFirstChar + stringLength) != '\000') {
        continue;
      }
      if (stringLength == (Offset)(strlen(pFirstChar))) {
        _tagHolder.TagAllocation(charsIndex, _charsTagIndex);
        check += 3;
      }
    }
  }
};
}  // namespace chap
