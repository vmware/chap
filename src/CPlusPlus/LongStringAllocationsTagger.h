// Copyright (c) 2019-2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/EdgePredicate.h"
#include "../Allocations/Graph.h"
#include "../Allocations/TagHolder.h"
#include "../Allocations/Tagger.h"
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class LongStringAllocationsTagger : public Allocations::Tagger<Offset> {
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
  static constexpr int NUM_OFFSETS_IN_HEADER = 4;
  LongStringAllocationsTagger(Graph& graph, TagHolder& tagHolder,
                              EdgePredicate& edgeIsTainted,
                              EdgePredicate& edgeIsFavored,
                              const ModuleDirectory<Offset>& moduleDirectory)
      : _graph(graph),
        _tagHolder(tagHolder),
        _edgeIsTainted(edgeIsTainted),
        _edgeIsFavored(edgeIsFavored),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _addressMap(graph.GetAddressMap()),
        _charsImage(_addressMap, _directory),
        _staticAnchorReader(_addressMap),
        _stackAnchorReader(_addressMap),
        _enabled(true),
        _tagIndex(_tagHolder.RegisterTag("%LongString", true, true)) {
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

    bool preCPlusPlus11ABIFound = false;
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
              return;
            }
            if (!strncmp(check, "_ZNSs6assign", 12)) {
              preCPlusPlus11ABIFound = true;
            }
          }
        }
      }
    }
    if (preCPlusPlus11ABIFound) {
      /*
       * We found no evidence that the C++ 11 ABI is present, but evidence
       * that the older ABI is.
       */
      _enabled = false;
      return;
    }
  }

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    if (!_enabled) {
      // The C++11 ABI doesn't appear to have been used in the process.
      return true;
    }
    if (_tagHolder.IsStronglyTagged(index)) {
      // Don't override any strong tags but do override weak ones.
      return true;  // We are finished looking at this allocation for this pass.
    }
    return TagAnchorPointLongStringChars(contiguousImage, index, phase,
                                         allocation);
  }

  bool TagFromReferenced(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         const AllocationIndex* unresolvedOutgoing) {
    if (!_enabled) {
      // The C++11 ABI doesn't appear to have been used in the process.
      return true;
    }
    return TagFromContainedStrings(index, contiguousImage, phase, allocation,
                                   unresolvedOutgoing);
  }

  TagIndex GetTagIndex() const { return _tagIndex; }

 private:
  Graph& _graph;
  TagHolder& _tagHolder;
  EdgePredicate& _edgeIsTainted;
  EdgePredicate& _edgeIsFavored;
  const Directory& _directory;
  AllocationIndex _numAllocations;
  const VirtualAddressMap<Offset>& _addressMap;
  ContiguousImage _charsImage;
  typename VirtualAddressMap<Offset>::Reader _staticAnchorReader;
  typename VirtualAddressMap<Offset>::Reader _stackAnchorReader;
  bool _enabled;
  TagIndex _tagIndex;

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
    const std::vector<Offset>* staticAnchors = _graph.GetStaticAnchors(index);
    const std::vector<Offset>* stackAnchors = _graph.GetStackAnchors(index);
    if (staticAnchors == nullptr && stackAnchors == nullptr) {
      return;
    }

    Offset address = allocation.Address();
    Offset size = allocation.Size();
    Offset stringLength = strnlen(contiguousImage.FirstChar(), size);
    if (stringLength == size) {
      return;
    }

    Offset minCapacity = _directory.MinRequestSize(index);
    if (minCapacity > 2 * sizeof(Offset)) {
      minCapacity--;
    } else {
      minCapacity = 2 * sizeof(Offset);
    }
    if (minCapacity < stringLength) {
      minCapacity = stringLength;
    }
    Offset maxCapacity = size - 1;

    if (!CheckLongStringAnchorIn(index, address, stringLength, minCapacity,
                                 maxCapacity, staticAnchors,
                                 _staticAnchorReader)) {
      CheckLongStringAnchorIn(index, address, stringLength, minCapacity,
                              maxCapacity, stackAnchors, _stackAnchorReader);
    }
  }
  bool CheckLongStringAnchorIn(AllocationIndex charsIndex, Offset charsAddress,
                               Offset stringLength, Offset minCapacity,
                               Offset maxCapacity,
                               const std::vector<Offset>* anchors,
                               Reader& anchorReader) {
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
        if ((capacity < minCapacity) || (capacity > maxCapacity)) {
          continue;
        }

        _tagHolder.TagAllocation(charsIndex, _tagIndex);
        _edgeIsTainted.SetAllOutgoing(charsIndex, true);

        return true;
      }
    }
    return false;
  }

  /*
   * Check whether the specified allocation contains any strings (but not the
   * C++11 ABI style that uses COW string bodies).  If so, for any of those
   * strings that are sufficiently long to use external buffers, tag the
   * external buffers.
   */
  bool TagFromContainedStrings(AllocationIndex index,
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
        CheckEmbeddedStrings(index, contiguousImage, unresolvedOutgoing);
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

  void CheckEmbeddedStrings(AllocationIndex index,
                            const ContiguousImage& contiguousImage,
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
      if (_tagHolder.IsStronglyTagged(charsIndex)) {
        // Don't override any strong tags but do override weak ones.
        continue;
      }
      Offset charsAddress = check[0];
      Offset stringLength = check[1];
      Offset capacity = check[2];

      if (capacity < 2 * sizeof(Offset)) {
        continue;
      }

      const Allocation* charsAllocation = _directory.AllocationAt(charsIndex);
      if (charsAllocation->Address() != charsAddress) {
        continue;
      }

      if (capacity > charsAllocation->Size() - 1) {
        continue;
      }

      if (stringLength < 2 * sizeof(Offset)) {
        continue;
      }

      if (stringLength > capacity) {
        continue;
      }

      _charsImage.SetIndex(charsIndex);
      const char* chars = _charsImage.FirstChar();
      if (chars[stringLength] != 0) {
        continue;
      }
      if (stringLength > 0 && (chars[stringLength - 1] == 0)) {
        continue;
      }

      if (capacity + 1 < _directory.MinRequestSize(charsIndex)) {
        /*
         * We want to assure that the capacity is sufficiently large
         * to account for the requested buffer size.  This depends
         * on the allocation directory to provide a lower bound of what
         * that requested buffer size might have been because this value
         * will differ depending on the type of allocator.
         */
        continue;
      }

      if (stringLength == (Offset)(strlen(chars))) {
        _tagHolder.TagAllocation(charsIndex, _tagIndex);
        _edgeIsTainted.SetAllOutgoing(charsIndex, true);
        _edgeIsFavored.Set(index, charsIndex, true);
        check += (NUM_OFFSETS_IN_HEADER - 1);
      }
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
