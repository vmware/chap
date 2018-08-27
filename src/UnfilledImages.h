// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "RangeMapper.h"
#include "VirtualAddressMap.h"

namespace chap {
template <typename OffsetType>
class UnfilledImages {
 public:
  typedef OffsetType Offset;
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef RangeMapper<Offset, const char *> UnfilledRanges;
  typedef typename UnfilledRanges::const_iterator UnfilledRangesConstIterator;

  UnfilledImages(const AddressMap &addressMap)
      : _addressMap(addressMap) {
  }

  const char *RegisterIfUnfilled(Offset base, Offset maxSize,
                                 const char *label) {
    base = base & ~0xfff;
    maxSize = maxSize & ~0xfff;
    typename VirtualAddressMap<Offset>::const_iterator it =
       _addressMap.find(base);
    if (it == _addressMap.end()) {
      return (const char *)(0);
    }
    const char *image = it.GetImage();
    if (image == (const char *)(0)) {
      return (const char *)(0);
    }
    int flags = it.Flags();
    if ((flags & VirtualAddressMap<Offset>::RangeAttributes::IS_WRITABLE) == 0) {
      return (const char *)(0);
    }
    Offset leftInRegion = it.Limit() - base;
    if (maxSize == 0 || maxSize > leftInRegion) {
      maxSize = leftInRegion;
    }
    Offset size = 0;
    for ( ; size < maxSize; size += sizeof(Offset)) {
      if (*((Offset *)(image + size)) != 0) {
        break;
      }
    }
    size = size & ~0xfff;
    if (size == 0) {
      return (const char *)(0);
    }
    if (_unfilledRanges.MapRange(base, size, label)) {
      return label;
    }
    UnfilledRangesConstIterator itUnfilled = _unfilledRanges.find(base);
    if (itUnfilled == _unfilledRanges.end()) {
      return (const char *)(0);
    }
    return itUnfilled->_value;
  }

  UnfilledRangesConstIterator find(Offset member) const {
    return _unfilledRanges.find(member);
  }

  bool IsUnfilled(Offset address) {
    return _unfilledRanges.find(address) != _unfilledRanges.end();
  }

  UnfilledRangesConstIterator end() const { return _unfilledRanges.end(); }

  const AddressMap &GetAddressMap() const { return _addressMap; }

 private:
  const AddressMap _addressMap;
  UnfilledRanges _unfilledRanges;
};

}  // namespace chap
