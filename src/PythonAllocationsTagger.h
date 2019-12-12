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
class PythonAllocationsTagger : public Allocations::Tagger<Offset> {
 public:
  typedef typename Allocations::Finder<Offset> Finder;
  typedef typename Allocations::Tagger<Offset> Tagger;
  typedef typename Tagger::Phase Phase;
  typedef typename Finder::AllocationIndex AllocationIndex;
  typedef typename Finder::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename TagHolder::TagIndex TagIndex;
  PythonAllocationsTagger(TagHolder& tagHolder,
                          const ModuleDirectory<Offset>& moduleDirectory)
      : _tagHolder(tagHolder),
        _tagIndex(_tagHolder.RegisterTag("%PyDictKeysObject")),
        _rangeToFlags(nullptr),
        _candidateBase(0),
        _candidateLimit(0),
        _enabled(false) {
    for (typename ModuleDirectory<Offset>::const_iterator it =
             moduleDirectory.begin();
         it != moduleDirectory.end(); ++it) {
      if (it->first.find("libpython3") != std::string::npos) {
        _rangeToFlags = &(it->second);
        _candidateBase = _rangeToFlags->begin()->_base;
        _candidateLimit = _rangeToFlags->rbegin()->_limit;
        _enabled = true;
        break;
      }
    }
  }

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& /* allocation */,
                         bool /* isUnsigned */) {
    if (!_enabled) {
      return true;  // There is nothing more to check.
    }
    if (_tagHolder.GetTagIndex(index) != 0) {
      /*
       * This was already tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation is not the root
       * node for a map or set.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        {
          const Offset* offsetLimit = contiguousImage.OffsetLimit();
          const Offset* offsets = contiguousImage.FirstOffset();
          if (offsetLimit - offsets < 5) {
            return true;
          }
          if (offsets[0] != 1) {
            /*
             * This is supposed to be a reference count of 1 because the
             * given object is considered to be exclusively owned.
             */
            return true;
          }
          Offset numSlots = offsets[1];
          if ((numSlots ^ (numSlots - 1)) != 2 * numSlots - 1) {
            // The number of slots must be a power of 2.
            return true;
          }

          if ((Offset)(((offsetLimit - offsets) - 5) / 3) < numSlots) {
            /*
             * The object wouldn't fit in the allocation.
             */
            return true;
          }
          Offset method = offsets[2];
          if (_candidateBase > method || method >= _candidateLimit) {
            return true;
          }

          if (_methods.find(method) == _methods.end()) {
            typename ModuleDirectory<Offset>::RangeToFlags::const_iterator it =
                _rangeToFlags->find(method);
            if (it == _rangeToFlags->end()) {
              return true;
            }
            int flags = it->_value;
            if ((flags &
                 (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE |
                  RangeAttributes::IS_EXECUTABLE)) !=
                (RangeAttributes::IS_READABLE |
                 RangeAttributes::IS_EXECUTABLE)) {
              return true;
            }
            _methods.insert(method);
          }
          _tagHolder.TagAllocation(index, _tagIndex);
          return true;  // No more checking is needed
        }
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

  TagIndex GetTagIndex() const { return _tagIndex; }

 private:
  TagHolder& _tagHolder;
  TagIndex _tagIndex;
  const typename ModuleDirectory<Offset>::RangeToFlags* _rangeToFlags;
  Offset _candidateBase;
  Offset _candidateLimit;
  bool _enabled;
  std::set<Offset> _methods;
};
}  // namespace chap
