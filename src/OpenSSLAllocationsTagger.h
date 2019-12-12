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
class OpenSSLAllocationsTagger : public Allocations::Tagger<Offset> {
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
  OpenSSLAllocationsTagger(TagHolder& tagHolder,
                           const ModuleDirectory<Offset>& moduleDirectory,
                           const VirtualAddressMap<Offset>& addressMap)
      : _tagHolder(tagHolder),
        _SSLTagIndex(_tagHolder.RegisterTag("%SSL")),
        _SSL_CTXTagIndex(_tagHolder.RegisterTag("%SSL_CTX")),
        _rangeToFlags(nullptr),
        _candidateBase(0),
        _candidateLimit(0),
        _enabled(false),
        _reader(addressMap) {
    for (typename ModuleDirectory<Offset>::const_iterator it =
             moduleDirectory.begin();
         it != moduleDirectory.end(); ++it) {
      if (it->first.find("libssl") != std::string::npos) {
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
          const Offset* firstOffset = contiguousImage.FirstOffset();
          if (offsetLimit - firstOffset < 0x40) {
            return true;
          }
          Offset firstCandidate = *firstOffset;
          Offset secondCandidate = *(firstOffset + 1);
          if (_candidateBase <= firstCandidate &&
              firstCandidate < _candidateLimit) {
            /*
             * It should be extremely rare that we reach this point without
             * a match, but some more checking is needed.
             */
            if (_methodsForSSL_CTX.find(firstCandidate) !=
                _methodsForSSL_CTX.end()) {
              _tagHolder.TagAllocation(index, _SSL_CTXTagIndex);
            } else if (CheckSSL_METHOD(firstCandidate)) {
              _tagHolder.TagAllocation(index, _SSL_CTXTagIndex);
              _methodsForSSL_CTX.insert(firstCandidate);
            }
          } else if (_candidateBase <= secondCandidate &&
                     secondCandidate < _candidateLimit) {
            /*
             * It should be extremely rare that we reach this point without
             * a match, but some more checking is needed.
             */
            if (_methodsForSSL.find(secondCandidate) != _methodsForSSL.end()) {
              _tagHolder.TagAllocation(index, _SSLTagIndex);
            } else if (CheckSSL_METHOD(secondCandidate)) {
              _tagHolder.TagAllocation(index, _SSLTagIndex);
              _methodsForSSL.insert(secondCandidate);
            }
          }
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

  TagIndex GetSSLTagIndex() const { return _SSLTagIndex; }
  TagIndex GetSSL_CTXTagIndex() const { return _SSL_CTXTagIndex; }

 private:
  TagHolder& _tagHolder;
  // const VirtualAddressMap<Offset>& _addressMap;
  TagIndex _SSLTagIndex;
  TagIndex _SSL_CTXTagIndex;
  const typename ModuleDirectory<Offset>::RangeToFlags* _rangeToFlags;
  Offset _candidateBase;
  Offset _candidateLimit;
  bool _enabled;
  std::set<Offset> _methodsForSSL_CTX;
  std::set<Offset> _methodsForSSL;
  Reader _reader;

  bool CheckSSL_METHOD(Offset candidate) {
    if (_rangeToFlags->find(candidate) == _rangeToFlags->end()) {
      return false;
    }

    Offset pMethod = candidate + sizeof(Offset);
    Offset firstMethod = _reader.ReadOffset(pMethod, 0xbad);
    if (firstMethod == 0xbad) {
      return false;
    }

    typename ModuleDirectory<Offset>::RangeToFlags::const_iterator it =
        _rangeToFlags->find(firstMethod);
    if (it == _rangeToFlags->end()) {
      return false;
    }

    int flags = it->_value;
    if ((flags & (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE |
                  RangeAttributes::IS_EXECUTABLE)) !=
        (RangeAttributes::IS_READABLE | RangeAttributes::IS_EXECUTABLE)) {
      return false;
    }
    Offset rangeBase = it->_base;
    Offset rangeLimit = it->_limit;
    for (Offset i = 1; i < 5; i++) {
      Offset methodPointer =
          _reader.ReadOffset(pMethod + (i * sizeof(Offset)), 0xbad);
      if (methodPointer < rangeBase || methodPointer >= rangeLimit) {
        return false;
      }
    }

    return true;
  }
};
}  // namespace chap
