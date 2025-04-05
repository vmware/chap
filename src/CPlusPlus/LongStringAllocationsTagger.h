// Copyright (c) 2019-2025 Broadcom. All Rights Reserved.
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
  LongStringAllocationsTagger(
      Graph& graph, TagHolder& tagHolder, EdgePredicate& edgeIsTainted,
      EdgePredicate& edgeIsFavored,
      const ModuleDirectory<Offset>& moduleDirectory,
      const typename Allocations::SignatureDirectory<Offset>&
          signatureDirectory)
      : _graph(graph),
        _tagHolder(tagHolder),
        _edgeIsTainted(edgeIsTainted),
        _edgeIsFavored(edgeIsFavored),
        _signatureDirectory(signatureDirectory),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _addressMap(graph.GetAddressMap()),
        _charsImage(_addressMap, _directory),
        _staticAnchorReader(_addressMap),
        _stackAnchorReader(_addressMap),
        _enabled(true),
        _tagIndex(_tagHolder.RegisterTag("%LongString", false, true)) {
    bool foundCheckableLibrary = false;
    for (const auto& nameAndModuleInfo : moduleDirectory) {
      if (nameAndModuleInfo.first.find("libstdc++.so.6") != std::string::npos) {
        foundCheckableLibrary = true;
      }
    }

    if (!foundCheckableLibrary) {
      return;
    }

    bool preCPlusPlus11ABIFound = false;
    for (const auto& nameAndModuleInfo : moduleDirectory) {
      for (const auto& range : nameAndModuleInfo.second._ranges) {
        if ((range._value._flags &
             ~VirtualAddressMap<Offset>::RangeAttributes::IS_EXECUTABLE) ==
            (VirtualAddressMap<Offset>::RangeAttributes::IS_READABLE |
             VirtualAddressMap<Offset>::RangeAttributes::HAS_KNOWN_PERMISSIONS |
             VirtualAddressMap<Offset>::RangeAttributes::IS_MAPPED)) {
          Offset base = range._base;
          Offset limit = range._limit;
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
                         bool isUnsigned) {
    if (!_enabled) {
      // The C++11 ABI doesn't appear to have been used in the process.
      return true;
    }
    if (_tagHolder.IsStronglyTagged(index)) {
      // Don't override any strong tags but do override weak ones.
      return true;  // We are finished looking at this allocation for this pass.
    }
    if (!isUnsigned) {
      /*
       * Unfortunately, there is a possibility of a string with large
       * capacity but short (<8) bytes length where the residue from
       * the previous usage of the buffer had a signature and the short
       * c-string imposed on the lower bits of the signature still leave
       * what looks like a signature.  For example, in the case that the
       * signature already has a low byte of 0x00 and there happens to
       * be a long string of 0 length, this might happen.  For now, in the
       * case of some ambiguity with an empty string, favor the signature.
       */
      if (*(contiguousImage.FirstChar()) == (const char)(0)) {
        return true;
      }
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
  const typename Allocations::SignatureDirectory<Offset>& _signatureDirectory;
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
        if (size <= 2 * sizeof(Offset)) {
          return true;
        }
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        if ((size > 0) && (size < 10 * sizeof(Offset))) {
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

    if (!CheckLongStringAnchorIn(contiguousImage, index, address, stringLength,
                                 minCapacity, maxCapacity, staticAnchors,
                                 _staticAnchorReader)) {
      CheckLongStringAnchorIn(contiguousImage, index, address, stringLength,
                              minCapacity, maxCapacity, stackAnchors,
                              _stackAnchorReader);
    }
  }
  bool CheckLongStringAnchorIn(const ContiguousImage& charsImage,
                               AllocationIndex charsIndex, Offset charsAddress,
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

        if ((stringLength < 2 * sizeof(Offset)) &&
            _addressMap.find(*(charsImage.FirstOffset())) !=
                _addressMap.end()) {
          /* A string short enough to fit in the header is also a sufficiently
           * week pattern that if we have something that looks like a pointer at
           * the start, the match is probably a coincidence.
           */
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

      /*
       * We cannot insist that the string length be >= 16 because the string
       * may have been shortened after the extra buffer was allocated but we
       * most definitely can insist that the capacity is large enough to
       * store a string of the given length and can check that the length
       * matches the actual length of the c-string.
       */

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

      if (stringLength != (Offset)(strlen(chars))) {
        continue;
      }

      if (stringLength == 0) {
        /*
         * Empty strings are such a weak pattern that we check to see whether
         * we simply have a signature with a low byte of 0, in which case
         * this is rejected as a long string.
         */
        Offset signatureCandidate = *(_charsImage.FirstOffset());
        if (_signatureDirectory.IsMapped(signatureCandidate)) {
          continue;
        }
      }
      if ((stringLength < 2 * sizeof(Offset)) &&
          _addressMap.find(*(_charsImage.FirstOffset())) != _addressMap.end()) {
        /* A string short enough to fit in the header is also a sufficiently
         * week pattern that if we have something that looks like a pointer at
         * the start, the match is probably a coincidence.
         */
        continue;
      }
      _tagHolder.TagAllocation(charsIndex, _tagIndex);
      _edgeIsTainted.SetAllOutgoing(charsIndex, true);
      _edgeIsFavored.Set(index, charsIndex, true);
      check += (NUM_OFFSETS_IN_HEADER - 1);
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
