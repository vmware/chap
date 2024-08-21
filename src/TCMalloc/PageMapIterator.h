// Copyright (c) 2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../VirtualAddressMap.h"

namespace chap {
namespace TCMalloc {
template <class Offset>
class PageMapIterator {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  PageMapIterator(const VirtualAddressMap<Offset>& addressMap, Offset pageMap,
                  Offset pageMapDepth, Offset firstMappedPage,
                  Offset lastMappedPage, bool simpleLeaf,
                  Offset firstPageFieldInSpan, Offset numPagesFieldInSpan,
                  Offset compactSizeClassFieldInSpan,
                  Offset locationAndSampledBitInSpan, Offset locationMask,
                  Offset sizeOfCompactSizeClass, Offset spansInLeaf,
                  Offset pageMapIndexBits, Offset middleNodeIndexBits,
                  Offset leafIndexBits, Offset pageOffsetBits, Offset sizes,
                  Offset numSizes)
      : _addressMap(addressMap),
        _pageMapReader(addressMap),
        _leafReader(addressMap),
        _spanReader(addressMap),
        _sizeReader(addressMap),
        _pageMap(pageMap),
        _pageMapDepth(pageMapDepth),
        _firstMappedPage(firstMappedPage),
        _lastMappedPage(lastMappedPage),
        _simpleLeaf(simpleLeaf),
        _firstPageFieldInSpan(firstPageFieldInSpan),
        _numPagesFieldInSpan(numPagesFieldInSpan),
        _compactSizeClassFieldInSpan(compactSizeClassFieldInSpan),
        _locationAndSampledBitInSpan(locationAndSampledBitInSpan),
        _locationMask(locationMask),
        _sizeOfCompactSizeClass(sizeOfCompactSizeClass),
        _spansInLeaf(spansInLeaf),
        _pageMapIndexBits(pageMapIndexBits),
        _middleNodeIndexBits(middleNodeIndexBits),
        _leafIndexBits(leafIndexBits),
        _pageOffsetBits(pageOffsetBits),
        _sizes(sizes),
        _numSizes(numSizes) {
    SetFirstNonEmptyPageFrom(_firstMappedPage);
  }

  ~PageMapIterator() {}

  /*
   * Return true if there are no more pages available.
   */
  bool Finished() { return _page > _lastMappedPage; }

  void Advance() {
    if (_page > _lastMappedPage) {
      return;
    }
    SetFirstNonEmptyPageFrom(_page + _numPagesForSpan);
  }

  Offset FirstPageForSpan() { return _page; }
  Offset NumPagesForSpan() { return _numPagesForSpan; }
  Offset FirstAddressForSpan() { return _firstAddressForSpan; }
  Offset SpanSize() { return _spanSize; }
  Offset AllocationSize() { return _allocationSize; }
  bool SpanIsUsed() { return _spanIsUsed; }
  Offset NumAllocationsInSpan() { return _numAllocationsInSpan; }
  Offset Span() { return _span; }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  typename VirtualAddressMap<Offset>::Reader _pageMapReader;
  typename VirtualAddressMap<Offset>::Reader _leafReader;
  typename VirtualAddressMap<Offset>::Reader _spanReader;
  typename VirtualAddressMap<Offset>::Reader _sizeReader;
  const Offset _pageMap;
  const Offset _pageMapDepth;
  const Offset _firstMappedPage;
  const Offset _lastMappedPage;
  const bool _simpleLeaf;
  const Offset _firstPageFieldInSpan;
  const Offset _numPagesFieldInSpan;
  const Offset _compactSizeClassFieldInSpan;
  const Offset _locationAndSampledBitInSpan;
  const unsigned char _locationMask;
  const Offset _sizeOfCompactSizeClass;
  const Offset _spansInLeaf;
  const Offset _pageMapIndexBits;
  const Offset _middleNodeIndexBits;
  const Offset _leafIndexBits;
  const Offset _pageOffsetBits;
  const Offset _sizes;
  const Offset _numSizes;

  /*
   * The following is information about the current span, if Finished() is
   * false.
   */

  Offset _page;
  Offset _numPagesForSpan;
  Offset _firstAddressForSpan;
  Offset _spanSize;
  Offset _numAllocationsInSpan;
  Offset _allocationSize;
  bool _spanIsUsed;
  Offset _span;

  void SetFirstNonEmptyPageFrom(Offset page) {
    if (page > _lastMappedPage) {
      _page = _lastMappedPage + 1;
      return;
    }
    Offset pagesPerLeaf = (1 << _leafIndexBits);
    Offset pageMapIndex = page >> _leafIndexBits;
    Offset leafIndex = page - (pageMapIndex << _leafIndexBits);
    Offset leaf =
        _pageMapReader.ReadOffset(_pageMap + pageMapIndex * sizeof(Offset), 0);
    _span = 0;
    while (true) {
      if (leaf == 0) {
        pageMapIndex++;
        leafIndex = 0;
        page = pageMapIndex * pagesPerLeaf;
        if (page > _lastMappedPage) {
          _page = _lastMappedPage + 1;
          return;
        }
        leaf = _pageMapReader.ReadOffset(
            _pageMap + pageMapIndex * sizeof(Offset), 0);
        continue;
      }
      _span = _leafReader.ReadOffset(
          leaf + _spansInLeaf + leafIndex * sizeof(Offset), 0);
      if (_span != 0) {
        break;
      }

      if (++leafIndex == pagesPerLeaf) {
        leaf = 0;
        continue;
      }
      page++;
    }
    _page = page;
    _firstAddressForSpan = page << _pageOffsetBits;
    _numAllocationsInSpan = 1;
    Offset firstPage = _spanReader.ReadOffset(_span + _firstPageFieldInSpan, 0);
    if (firstPage == page) {
      _numPagesForSpan =
          _spanReader.ReadOffset(_span + _numPagesFieldInSpan, 0);
      _spanSize = _numPagesForSpan << _pageOffsetBits;
      unsigned char locationAndSampledBit =
          _spanReader.ReadU8(_span + _locationAndSampledBitInSpan, 0xff);
      _spanIsUsed = ((locationAndSampledBit & _locationMask) == 0);
      if (_spanIsUsed) {
        Offset compactSizeClass =
            _simpleLeaf
                ? _spanReader.ReadU8(_span + _compactSizeClassFieldInSpan, 0)
                : (_sizeOfCompactSizeClass == 1)
                      ? _leafReader.ReadU8(leaf + leafIndex, 0)
                      : _leafReader.ReadU16(leaf + 2 * leafIndex, 0);
        if (compactSizeClass > 0 && compactSizeClass < _numSizes) {
          Offset innerAllocationSize =
              (Offset)_sizeReader.ReadU32(_sizes + 4 * compactSizeClass, 0);
          if (innerAllocationSize <= _spanSize) {
            _numAllocationsInSpan = _spanSize / innerAllocationSize;
            _allocationSize = innerAllocationSize;
            return;
          }
        }
      }
      _allocationSize = _spanSize;

      return;
    }
    _numPagesForSpan = 1;
    _allocationSize = (1 << _pageOffsetBits);
    _spanSize = _allocationSize;
    _spanIsUsed = false;
  }
};

}  // namespace TCMalloc
}  // namespace chap
