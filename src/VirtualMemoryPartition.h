// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
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
  typedef RangeMapper<Offset, const char *> ClaimedRanges;
  typedef typename ClaimedRanges::const_iterator ClaimedRangesConstIterator;
  typedef RangeMapper<Offset, int> UnclaimedImages;
  typedef typename UnclaimedImages::const_iterator UnclaimedImagesConstIterator;

  VirtualMemoryPartition(const AddressMap &addressMap)
      : _addressMap(addressMap) {
    typename AddressMap::const_iterator itEnd = _addressMap.end();
    for (typename AddressMap::const_iterator it = _addressMap.begin();
         it != itEnd; ++it) {
      const char *image = it.GetImage();
      if (image != (const char *)(0)) {
        _unclaimedImages.MapRange(it.Base(), it.Size(), it.Flags());
      }
    }
  }

  bool ClaimRange(Offset base, Offset size, const char *label) {
    if (!_claimedRanges.MapRange(base, size, label)) {
      return false;  // The range overlaps a claimed range.
    }
    _unclaimedImages.UnmapRange(base, size);
    return true;
  }

  UnclaimedImagesConstIterator BeginUnclaimedImages() const {
    return _unclaimedImages.begin();
  }

  UnclaimedImagesConstIterator EndUnclaimedImages() const {
    return _unclaimedImages.end();
  }

  ClaimedRangesConstIterator find(Offset member) const {
    return _claimedRanges.find(member);
  }

  ClaimedRangesConstIterator end() const { return _claimedRanges.end(); }

  const AddressMap &GetAddressMap() { return _addressMap; }

 private:
  const AddressMap _addressMap;
  ClaimedRanges _claimedRanges;
  UnclaimedImages _unclaimedImages;
};

}  // namespace chap
