// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "RangeMapper.h"
#include "VirtualAddressMap.h"

namespace chap {
template <typename OffsetType>
class VirtualMemoryPartition {
 public:
  typedef OffsetType Offset;
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef typename AddressMap::RangeAttributes RangeAttributes;
  typedef RangeMapper<Offset, const char *> ClaimedRanges;
  typedef typename ClaimedRanges::const_iterator ClaimedRangesConstIterator;
  typedef RangeMapper<Offset, int> RangesWithFlags;

  VirtualMemoryPartition(const AddressMap &addressMap)
      : UNKNOWN("unknown"),
        _addressMap(addressMap),
        _claimedWritableRanges(false),
        _claimedRxOnlyRanges(false),
        _claimedReadOnlyRanges(false),
        _claimedInaccessibleRanges(false),
        _unclaimedWritableRanges(true),
        _unclaimedRxOnlyRanges(true),
        _unclaimedReadOnlyRanges(true),
        _unclaimedInaccessibleRanges(true) {
    typename AddressMap::const_iterator itEnd = _addressMap.end();
    for (typename AddressMap::const_iterator it = _addressMap.begin();
         it != itEnd; ++it) {
      const char *image = it.GetImage();
      int flags = it.Flags();
      if ((flags & RangeAttributes::IS_WRITABLE) != 0) {
        _unclaimedWritableRanges.MapRange(it.Base(), it.Size(), flags);
        if (image != nullptr) {
          _unclaimedWritableRangesWithImages.MapRange(it.Base(), it.Size(),
                                                      flags);
          _staticAnchorCandidates.MapRange(it.Base(), it.Size(), flags);
        }
      } else if ((flags & RangeAttributes::IS_EXECUTABLE) != 0) {
        _unclaimedRxOnlyRanges.MapRange(it.Base(), it.Size(), flags);
      } else if ((flags & RangeAttributes::IS_READABLE) != 0) {
        _unclaimedReadOnlyRanges.MapRange(it.Base(), it.Size(), flags);
      } else {
        _unclaimedInaccessibleRanges.MapRange(it.Base(), it.Size(), flags);
      }
    }
  }

  bool ClaimRange(Offset base, Offset size, const char *label,
                  bool staticAnchorCandidate) {
    if (!_claimedRanges.MapRange(base, size, label)) {
      return false;  // The range overlaps a claimed range.
    }
    Offset limit = base + size;
    /*
     * Find the first range with limit not less than the base address.
     */
    auto it = _addressMap.lower_bound(base);
    auto itEnd = _addressMap.end();
    if (it == itEnd || it.Base() >= limit) {
      /*
       * The range is not mentioned in the virtual address map.  Assume that it
       * was omitted and hence likely to be inaccessible.  It is rather common
       * for recent core files to omit inaccessible regions entirely.
       */
      _claimedInaccessibleRanges.MapRange(base, size, label);
      _unclaimedInaccessibleRanges.UnmapRange(base, size);
      return true;
    }
    int flags = it.Flags();
    if ((flags & RangeAttributes::IS_WRITABLE) != 0) {
      _unclaimedWritableRangesWithImages.UnmapRange(base, size);
      if (!staticAnchorCandidate) {
        ClearStaticAnchorCandidates(base, size);
      }
      _claimedWritableRanges.MapRange(base, size, label);
      _unclaimedWritableRanges.UnmapRange(base, size);
    } else if ((flags & RangeAttributes::IS_EXECUTABLE) != 0) {
      _claimedRxOnlyRanges.MapRange(base, size, label);
      _unclaimedRxOnlyRanges.UnmapRange(base, size);
    } else if ((flags & RangeAttributes::IS_READABLE) != 0) {
      _claimedReadOnlyRanges.MapRange(base, size, label);
      _unclaimedReadOnlyRanges.UnmapRange(base, size);
    } else {
      _claimedInaccessibleRanges.MapRange(base, size, label);
      _unclaimedInaccessibleRanges.UnmapRange(base, size);
    }
    return true;
  }

  void ClaimUnclaimedRangesAsUnknown() {
    for (const auto &range : _unclaimedWritableRanges) {
      _claimedWritableRanges.MapRange(range._base, range._size, UNKNOWN);
    }
    _unclaimedWritableRanges.clear();
    for (const auto &range : _unclaimedRxOnlyRanges) {
      _claimedRxOnlyRanges.MapRange(range._base, range._size, UNKNOWN);
    }
    _unclaimedRxOnlyRanges.clear();
    for (const auto &range : _unclaimedReadOnlyRanges) {
      _claimedReadOnlyRanges.MapRange(range._base, range._size, UNKNOWN);
    }
    _unclaimedReadOnlyRanges.clear();
    for (const auto &range : _unclaimedInaccessibleRanges) {
      _claimedInaccessibleRanges.MapRange(range._base, range._size, UNKNOWN);
    }
    _unclaimedInaccessibleRanges.clear();
  }
  void ClearStaticAnchorCandidates(Offset base, Offset size) {
    _staticAnchorCandidates.UnmapRange(base, size);
  }

  const RangesWithFlags &GetUnclaimedWritableRangesWithImages() const {
    return _unclaimedWritableRangesWithImages;
  }

  const RangesWithFlags &GetStaticAnchorCandidates() const {
    return _staticAnchorCandidates;
  }

  const ClaimedRanges &GetClaimedWritableRanges() const {
    return _claimedWritableRanges;
  }

  const ClaimedRanges &GetClaimedRXOnlyRanges() const {
    return _claimedRxOnlyRanges;
  }

  const ClaimedRanges &GetClaimedReadOnlyRanges() const {
    return _claimedReadOnlyRanges;
  }

  const ClaimedRanges &GetClaimedInaccessibleRanges() const {
    return _claimedInaccessibleRanges;
  }

  void DumpClaimedRanges() {
    for (const auto &range : _claimedRanges) {
      std::cerr << "[0x" << std::hex << range._base << ", 0x" << range._limit
                << ") \"" << range._value << "\"\n";
    }
  }
  ClaimedRangesConstIterator find(Offset member) const {
    return _claimedRanges.find(member);
  }

  bool IsClaimed(Offset address) {
    return _claimedRanges.find(address) != _claimedRanges.end();
  }

  ClaimedRangesConstIterator end() const { return _claimedRanges.end(); }

  const AddressMap &GetAddressMap() const { return _addressMap; }

  const char *UNKNOWN;

 private:
  const AddressMap &_addressMap;
  ClaimedRanges _claimedRanges;
  ClaimedRanges _claimedWritableRanges;
  ClaimedRanges _claimedRxOnlyRanges;
  ClaimedRanges _claimedReadOnlyRanges;
  ClaimedRanges _claimedInaccessibleRanges;
  RangesWithFlags _unclaimedWritableRanges;
  RangesWithFlags _unclaimedRxOnlyRanges;
  RangesWithFlags _unclaimedReadOnlyRanges;
  RangesWithFlags _unclaimedInaccessibleRanges;
  RangesWithFlags _unclaimedWritableRangesWithImages;
  RangesWithFlags _staticAnchorCandidates;
};

}  // namespace chap
