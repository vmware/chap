// Copyright (c) 2018 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "RangeMapper.h"
#include "VirtualAddressMap.h"

namespace chap {
template <typename OffsetType>
class PermissionsConstrainedRanges {
 public:
  typedef OffsetType Offset;
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef RangeMapper<Offset, const char *> Ranges;
  typedef typename Ranges::const_iterator const_iterator;
  typedef typename Ranges::const_reverse_iterator const_reverse_iterator;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;

  PermissionsConstrainedRanges(const AddressMap &addressMap,
                               int permissionsMask, int expectedPermissionsBits,
                               bool rangesMustBeKnownInProcessImage)
      : _addressMap(addressMap),
        _permissionsMask(permissionsMask),
        _expectedPermissionsBits(expectedPermissionsBits),
        _rangesMustBeKnownInProcessImage(rangesMustBeKnownInProcessImage) {
    typename AddressMap::const_iterator itEnd = _addressMap.end();
    for (typename AddressMap::const_iterator it = _addressMap.begin();
         it != itEnd; ++it) {
      if ((it.Flags() & _permissionsMask) == _expectedPermissionsBits) {
        /*
         * The permissions are known and this region is read only.
         */
        _ranges.MapRange(it.Base(), it.Size(), (const char *)(0));
      }
    }
  }

  /*
   * The caller is asserting ownership of the given range and supplying
   * a label for use in summaries of ranges that match the permissions
   * constraints that were given in the constructor.
   */
  bool ClaimRange(Offset base, Offset size, const char *label) {
    Offset limit = base + size;
    typename AddressMap::const_iterator itMapEnd = _addressMap.end();
    if (_rangesMustBeKnownInProcessImage) {
      /*
       * Make sure the range to be claimed is fully covered by one or more
       * known ranges from the virtual address map, not necessarily with
       * images in the process image.
       */
      typename AddressMap::const_iterator itMap = _addressMap.lower_bound(base);
      if (itMap == itMapEnd || itMap.Base() > base) {
        return false;
      }
      while (true) {
        if ((itMap.Flags() & _permissionsMask) != _expectedPermissionsBits) {
          return false;
        }

        if (itMap.Limit() >= limit) {
          break;
        }

        if ((++itMap == itMapEnd) || itMap.Base() != limit) {
          return false;
        }
      }
    } else {
      /*
       * Do not insist on coverage of the requested range by ranges from
       * the virtual address map, but do make sure that the requested range
       * is not overlapped by any ranges from the map that do not satisfy
       * the permissions constraints.
       */
      for (typename AddressMap::const_iterator itMap =
               _addressMap.lower_bound(base);
           itMap != itMapEnd && itMap.Base() < limit; ++itMap) {
        if ((itMap.Flags() & _permissionsMask) != _expectedPermissionsBits) {
          return false;
        }
      }
    }

    /*
     * Make sure that no claimed ranges overlap the newly claimed range
     * and fail if so.
     */
    const_iterator itEnd = _ranges.end();
    for (const_iterator it = _ranges.lower_bound(base);
         it != itEnd && it->_base < limit; ++it) {
      if (it->_value != 0) {
        return false;
      }
    }
    /*
     * Unmap any overlap with unclaimed ranges.
     */

    _ranges.UnmapRange(base, size);

    if (!_ranges.MapRange(base, size, label)) {
      abort();  // We don't expect any overlap at this point.
    }
    return true;
  }

  const_iterator begin() const { return _ranges.begin(); }

  const_iterator end() const { return _ranges.end(); }

  const_reverse_iterator rbegin() const { return _ranges.rbegin(); }

  const_reverse_iterator rend() const { return _ranges.rend(); }

  const_iterator find(Offset member) const { return _ranges.find(member); }

  const AddressMap &GetAddressMap() const { return _addressMap; }

 private:
  const AddressMap _addressMap;
  int _permissionsMask;
  int _expectedPermissionsBits;
  bool _rangesMustBeKnownInProcessImage;
  Ranges _ranges;
};

}  // namespace chap
