// Copyright (c) 2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../VirtualAddressMap.h"

namespace chap {
namespace GoLang {
template <class Offset>
class MappedPageRangeIterator {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  MappedPageRangeIterator(const VirtualAddressMap<Offset>& addressMap,
                          Offset arenasFieldValue, Offset spansInHeapArena,
                          Offset arenasIndexBits, Offset spansIndexBits,
                          Offset pageOffsetBits, Offset startAddrInMspan,
                          Offset numPagesInMspan, Offset firstMappedPage,
                          Offset lastMappedPage)
      : _addressMap(addressMap),
        _arenasArrayReader(addressMap),
        _spansArrayReader(addressMap),
        _spanReader(addressMap),
        _arenasFieldValue(arenasFieldValue),
        _spansInHeapArena(spansInHeapArena),
        _arenasIndexBits(arenasIndexBits),
        _spansIndexBits(spansIndexBits),
        _pageOffsetBits(pageOffsetBits),
        _startAddrInMspan(startAddrInMspan),
        _numPagesInMspan(numPagesInMspan),
        _firstMappedPage(firstMappedPage),
        _lastMappedPage(lastMappedPage)

  {
    SetFirstNonEmptyPageFrom(_firstMappedPage);
  }

  ~MappedPageRangeIterator() {}

  /*
   * Return true if there are no more pages available.
   */
  bool Finished() { return _page > _lastMappedPage; }

  void Advance() {
    if (_page > _lastMappedPage) {
      return;
    }
    SetFirstNonEmptyPageFrom(_page + _numPagesForRange);
  }

  Offset FirstPageForRange() { return _page; }
  Offset NumPagesForRange() { return _numPagesForRange; }
  Offset RangeSize() { return _rangeSize; }
  Offset FirstAddressForRange() { return _firstAddressForRange; }
  Offset Mspan() { return _mspan; }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  typename VirtualAddressMap<Offset>::Reader _arenasArrayReader;
  typename VirtualAddressMap<Offset>::Reader _spansArrayReader;
  typename VirtualAddressMap<Offset>::Reader _spanReader;
  Offset _arenasFieldValue;
  Offset _spansInHeapArena;
  Offset _arenasIndexBits;
  Offset _spansIndexBits;
  Offset _pageOffsetBits;
  Offset _startAddrInMspan;
  Offset _numPagesInMspan;
  Offset _firstMappedPage;
  Offset _lastMappedPage;

  /*
   * The following is information about the current range, if Finished() is
   * false.
   */

  Offset _page;
  Offset _firstAddressForRange;
  Offset _numPagesForRange;
  Offset _rangeSize;
  Offset _mspan;

  void SetFirstNonEmptyPageFrom(Offset page) {
    if (page > _lastMappedPage) {
      _page = _lastMappedPage + 1;
      return;
    }
    Offset pagesPerHeapArena = (1 << _spansIndexBits);
    Offset arenasIndex = page >> _spansIndexBits;
    Offset spansIndex = page - (arenasIndex << _spansIndexBits);
    Offset arenasArray = _arenasFieldValue + 0x1000000;
    Offset heapArena =
        _arenasArrayReader.ReadOffset(arenasArray + arenasIndex * sizeof(Offset), 0);
    _mspan = 0;
    while (true) {
      if (heapArena == 0) {
        arenasIndex++;
        spansIndex = 0;
        page = arenasIndex * pagesPerHeapArena;
        if (page > _lastMappedPage) {
          _page = _lastMappedPage + 1;
          return;
        }
        heapArena = _arenasArrayReader.ReadOffset(
            arenasArray + arenasIndex * sizeof(Offset), 0);
        continue;
      }
      _mspan = _spansArrayReader.ReadOffset(
          heapArena + _spansInHeapArena + spansIndex * sizeof(Offset), 0);
      if (_mspan != 0) {
        break;
      }

      if (++spansIndex == pagesPerHeapArena) {
        heapArena = 0;
        continue;
      }
      page++;
    }
    _page = page;
    _firstAddressForRange = page << _pageOffsetBits;
    Offset startAddr = _spanReader.ReadOffset(_mspan + _startAddrInMspan, 0);
    if (startAddr == _firstAddressForRange) {
      _numPagesForRange =
          _spanReader.ReadOffset(_mspan + _numPagesInMspan, 0);
      _rangeSize = _numPagesForRange << _pageOffsetBits;

      return;
    }
    _numPagesForRange = 1;
    _rangeSize = (1 << _pageOffsetBits);
    _mspan = 0;
  }
};

}  // namespace GoLang
}  // namespace chap
