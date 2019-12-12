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
class COWStringAllocationsTagger : public Allocations::Tagger<Offset> {
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
  COWStringAllocationsTagger(Graph& graph, TagHolder& tagHolder,
                             const ModuleDirectory<Offset>& moduleDirectory)
      : _graph(graph),
        _tagHolder(tagHolder),
        _finder(graph.GetAllocationFinder()),
        _numAllocations(_finder.NumAllocations()),
        _addressMap(_finder.GetAddressMap()),
        _charsImage(_finder),
        _staticAnchorReader(_addressMap),
        _stackAnchorReader(_addressMap),
        _enabled(true),
        _tagIndex(_tagHolder.RegisterTag("%COWStringBody")) {
    _votesNeeded.resize(_numAllocations, 0xff);
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

    bool found_ZN = false;
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
            if (!strncmp(check, "_ZNSs6assign", 12)) {
              return;
            }
            if (!strncmp(check, "_ZN", 3)) {
              found_ZN = true;
            }
          }
        }
      }
    }
    if (found_ZN) {
      _enabled = false;
    }
  }

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool isUnsigned) {
    if (!_enabled) {
      // A pre-C++11 ABI doesn't appear to have been used in the process.
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
    if (!isUnsigned) {
      /*
       * For now, assume that the size field of a string will never match
       * a value that would be interpreted as a signature.  This is just
       * as a performance enhancement and it can be removed if it is
       * determined to introduce any false negatives.
       */
      return true;
    }
    return TagAnchorPointCOWStringBody(contiguousImage, index, phase,
                                       allocation);
  }

  bool TagFromReferenced(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex /* index */,
                         Phase phase, const Allocation& allocation,
                         const AllocationIndex* unresolvedOutgoing) {
    if (!_enabled) {
      // A pre-C++11 ABI doesn't appear to have been used in the process.
      return true;
    }
    return TagFromContainedStrings(contiguousImage, phase, allocation,
                                   unresolvedOutgoing);
  }

  TagIndex GetTagIndex() const { return _tagIndex; }

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
  TagIndex _tagIndex;
  std::vector<uint8_t> _votesNeeded;
  Offset _stringLength;    // valid only during TagFromAllocation
  int32_t _numRefsMinus1;  // valid only during TagFromAllocation

  /*
   * Check whether the specified allocation holds a long string, for the current
   * style of strings without COW string bodies, where the std::string is
   * on the stack or statically allocated, tagging it if so.
   * Return true if no further work is needed to check.
   */
  bool TagAnchorPointCOWStringBody(const ContiguousImage& contiguousImage,
                                   AllocationIndex index, Phase phase,
                                   const Allocation& allocation) {
    Offset size = allocation.Size();

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK: {
        // Fast initial check, match must be solid
        Offset overhead = 3 * sizeof(Offset) + 1;
        const Offset* firstOffset = contiguousImage.FirstOffset();
        if (size < overhead) {
          return true;
        }
        Offset capacity = firstOffset[1];
        if (capacity == 0) {
          /*
           * A COWStringBody that can't store anything is unlikely, because
           * there is typically a statically allocated buffer to handle such
           * cases.  In any case we don't allow it here, to avoid false
           * positives.
           */
          return true;
        }
        if (capacity > size - overhead) {
          /*
           * The allocation isn't big enough to have that capacity.
           */
          return true;
        }

        _stringLength = firstOffset[0];
        if (_stringLength > capacity) {
          return true;
        }
        const char* chars = (char*)(firstOffset + 3);
        if (chars[_stringLength] != 0) {
          return true;
        }
        if (_stringLength > 0 && (chars[_stringLength - 1] == 0)) {
          return true;
        }

        _numRefsMinus1 = *((int32_t*)(firstOffset + 2));
        if (_numRefsMinus1 < 0) {
          return true;
        }
        if (capacity + overhead < _finder.MinRequestSize(index)) {
          /*
           * We want to assure that the capacity is sufficiently large
           * to account for the requested buffer size, but this depends
           * on the allocation finder to provide a lower bound of what
           * that requested buffer size might have been.
           */
          return true;
        }
      } break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        if (size < 10 * sizeof(Offset)) {
          if (strlen(contiguousImage.FirstChar() + 3 * sizeof(Offset)) ==
              _stringLength) {
            _votesNeeded[index] =
                (_numRefsMinus1 < 0x10) ? (_numRefsMinus1 + 1) : 0x10;

            TallyAnchorVotes(index, allocation);
          }
          return true;
        }
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        if (strlen(contiguousImage.FirstChar() + 3 * sizeof(Offset)) ==
            _stringLength) {
          _votesNeeded[index] =
              (_numRefsMinus1 < 0x10) ? (_numRefsMinus1 + 1) : 0x10;

          TallyAnchorVotes(index, allocation);
        }
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

  void TallyAnchorVotes(AllocationIndex index, const Allocation& allocation) {
    if (!TallyAnchorVotes(index, allocation, _graph.GetStaticAnchors(index),
                          _staticAnchorReader)) {
      TallyAnchorVotes(index, allocation, _graph.GetStackAnchors(index),
                       _stackAnchorReader);
    }
  }
  bool TallyAnchorVotes(AllocationIndex bodyIndex,
                        const Allocation& bodyAllocation,

                        const std::vector<Offset>* anchors,
                        Reader& anchorReader) {
    Offset charsAddress = bodyAllocation.Address() + (3 * sizeof(Offset));
    if (anchors != nullptr) {
      for (Offset anchor : *anchors) {
        if (anchorReader.ReadOffset(anchor, 0xbad) == charsAddress) {
          if (--(_votesNeeded[bodyIndex]) == 0) {
            _tagHolder.TagAllocation(bodyIndex, _tagIndex);
            return true;
          }
        }
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
        return allocation.Size() < sizeof(Offset);
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
    const Offset* checkLimit = contiguousImage.OffsetLimit();
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
      if (_votesNeeded[charsIndex] == 0xff) {
        continue;
      }
      Offset charsAddress = check[0];
      if ((_finder.AllocationAt(charsIndex)->Address() + 3 * sizeof(Offset)) ==
          charsAddress) {
        if (--(_votesNeeded[charsIndex]) == 0) {
          _tagHolder.TagAllocation(charsIndex, _tagIndex);
        }
      }
    }
  }
};
}  // namespace chap
