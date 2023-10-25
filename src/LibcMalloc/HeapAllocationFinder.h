// Copyright (c) 2017-2020,2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../VirtualAddressMap.h"
#include "CorruptionSkipper.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace LibcMalloc {
template <class Offset>
class HeapAllocationFinder : public Allocations::Directory<Offset>::Finder {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;

  HeapAllocationFinder(const VirtualAddressMap<Offset>& addressMap,
                       const InfrastructureFinder<Offset>& infrastructureFinder,
                       CorruptionSkipper<Offset>& corruptionSkipper,
                       FastBinFreeStatusFixer<Offset>& fastBinFreeStatusFixer,
                       DoublyLinkedListCorruptionChecker<Offset>&
                           doublyLinkedListCorruptionChecker,
                       Allocations::Directory<Offset>& allocationDirectory)
      : _addressMap(addressMap),
        _reader(addressMap),
        _infrastructureFinder(infrastructureFinder),
        _arenas(_infrastructureFinder.GetArenas()),
        _mainArenaAddress(_infrastructureFinder.GetMainArenaAddress()),
        _arenaStructSize(_infrastructureFinder.GetArenaStructSize()),
        _maxHeapSize(_infrastructureFinder.GetMaxHeapSize()),
        _heapHeaderSize(_infrastructureFinder.GetHeapHeaderSize()),
        _heapMap(_infrastructureFinder.GetHeaps()),
        _heapMapIterator(_heapMap.begin()),
        _corruptionSkipper(corruptionSkipper),
        _fastBinFreeStatusFixer(fastBinFreeStatusFixer),
        _doublyLinkedListCorruptionChecker(doublyLinkedListCorruptionChecker) {
    if (_heapMapIterator != _heapMap.end()) {
      SkipHeaders();
      Advance();
    }
    _finderIndex = allocationDirectory.AddFinder(this);
  }

  virtual ~HeapAllocationFinder() {}

  /*
   * Return true if there are no more allocations available.
   */
  virtual bool Finished() { return _heapMapIterator == _heapMap.end(); }

  /*
   * Return the address of the next allocation (in increasing order of
   * address) to be reported by this finder, without advancing to the next
   * allocation.  The return value is undefined if there are no more
   * allocations available.  Note that at the time this function is called
   * any allocations already reported by this allocation finder have already
   * been assigned allocation indices in the Directory.
   */
  virtual Offset NextAddress() { return _allocationAddress; }
  /*
   * Return the size of the next allocation (in increasing order of
   * address) to be reported by this finder, without advancing to the next
   * allocation.  The return value is undefined if there are no more
   * allocations available.
   */
  virtual Offset NextSize() { return _allocationSize; }
  /*
   * Return true if the next allocation (in increasing order of address) to
   * address) to be reported by this finder is considered used, without
   * advancing to the next allocation.
   */
  virtual bool NextIsUsed() { return _allocationIsUsed; }
  /*
   * Advance to the next allocation.
   */
  virtual void Advance() {
    if (_heapMapIterator != _heapMap.end()) {
      while (!AdvanceToNextAllocationOfHeap()) {
        if (++_heapMapIterator == _heapMap.end()) {
          for (auto keyAndValue : _arenas) {
            if (keyAndValue.first != _mainArenaAddress) {
              const typename InfrastructureFinder<Offset>::Arena& arena =
                  keyAndValue.second;
              _fastBinFreeStatusFixer.MarkFastBinItemsAsFree(arena, false,
                                                             _finderIndex);
              _doublyLinkedListCorruptionChecker
                  .CheckDoublyLinkedListCorruption(arena);
            }
          }
          return;
        } else {
          SkipHeaders();
        }
      }
    }
  }
  /*
   * Return the smallest request size that might reasonably have resulted
   * in an allocation of the given size.
   */
  virtual Offset MinRequestSize(Offset size) {
    return (size <= 5 * sizeof(Offset)) ? 0 : (size - 0x1f);
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  typename VirtualAddressMap<Offset>::Reader _reader;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const typename InfrastructureFinder<Offset>::ArenaMap& _arenas;
  const Offset _mainArenaAddress;
  const Offset _arenaStructSize;
  const Offset _maxHeapSize;
  const Offset _heapHeaderSize;
  const typename InfrastructureFinder<Offset>::HeapMap& _heapMap;
  typename InfrastructureFinder<Offset>::HeapMap::const_iterator
      _heapMapIterator;
  Offset _allocationAddress;
  Offset _allocationSize;
  bool _allocationIsUsed;
  Offset _base;
  Offset _limit;
  Offset _chunkSize;
  Offset _prevCheck;
  Offset _check;
  Offset _checkLimit;
  Offset _sizeAndFlags;
  Offset _top;
  CorruptionSkipper<Offset>& _corruptionSkipper;
  FastBinFreeStatusFixer<Offset>& _fastBinFreeStatusFixer;
  DoublyLinkedListCorruptionChecker<Offset>& _doublyLinkedListCorruptionChecker;
  size_t _finderIndex;

  void SkipHeaders() {
    const typename InfrastructureFinder<Offset>::Heap& heap =
        _heapMapIterator->second;
    _base = heap._address;
    Offset size = heap._size;
    const char* heapImage;
    Offset numBytesFound = _addressMap.FindMappedMemoryImage(_base, &heapImage);
    if (numBytesFound < size) {
      std::cerr << "Heap at 0x" << std::hex << _base
                << " is not fully mapped in the core.\n";
      size = numBytesFound;
    }
    _limit = _base + size;

    if ((heap._arenaAddress & ~(_maxHeapSize - 1)) == _base) {
      _base += _heapHeaderSize + _arenaStructSize;
    } else {
      _base += _heapHeaderSize;
    }

    _top = 0;
    typename InfrastructureFinder<Offset>::ArenaMap::const_iterator itArena =
        _arenas.find(heap._arenaAddress);
    if (itArena == _arenas.end()) {
      abort();
    }
    _top = itArena->second._top;

    _sizeAndFlags = _reader.ReadOffset(_base + sizeof(Offset));
    _chunkSize = 0;
    _prevCheck = _base;
    _check = _base;
    _checkLimit = _limit - 4 * sizeof(Offset);
  }

  bool AdvanceToNextAllocationOfHeap() {
    const typename InfrastructureFinder<Offset>::Heap& heap =
        _heapMapIterator->second;
    while (_check < _checkLimit) {
      if (((_sizeAndFlags & 2) != 0) ||
          ((sizeof(Offset) == 8) && ((_sizeAndFlags & sizeof(Offset)) != 0))) {
        _check = HandleNonMainArenaCorruption(heap, _prevCheck);
        if (_check != 0) {
          _chunkSize = 0;
          _sizeAndFlags = _reader.ReadOffset(_check + sizeof(Offset), 0xbadbad);
          if (_sizeAndFlags != 0xbadbad) {
            _prevCheck = _check;
            continue;
          }
        }
        return false;
      }
      _chunkSize = _sizeAndFlags & ~7;
      if ((_chunkSize == 0) || (_chunkSize >= 0x10000000) ||
          (_chunkSize > (_limit - _check))) {
        _check = HandleNonMainArenaCorruption(heap, _prevCheck);
        if (_check != 0) {
          _chunkSize = 0;
          _sizeAndFlags = _reader.ReadOffset(_check + sizeof(Offset), 0xbadbad);
          if (_sizeAndFlags != 0xbadbad) {
            _prevCheck = _check;
            continue;
          }
        }
        return false;
      }
      _allocationSize = _chunkSize - sizeof(Offset);
      bool isFree = true;
      if (_check + _chunkSize == _limit) {
        _allocationSize -= sizeof(Offset);
      } else {
        _sizeAndFlags =
            _reader.ReadOffset(_check + sizeof(Offset) + _chunkSize, 0xbadbad);
        if (_sizeAndFlags == 0xbadbad) {
          return false;
        }
        isFree = ((_sizeAndFlags & 1) == 0) ||
                 (_allocationSize < 3 * sizeof(Offset));
      }
      if ((_check + _allocationSize + 3 * sizeof(Offset) == _limit) &&
          ((_sizeAndFlags & ~7) == 0)) {
        break;
      }
      _allocationAddress = _check + 2 * sizeof(Offset);
      if (isFree) {
        if (_check == _top) {
          /*
           * If the entry is the top value for an arena, we want the size of the
           * allocation to include any writable bytes in the heap that follow
           * the top allocation so that the results of "count free" reflect
           * the bytes that are actually available for allocation.  Otherwise,
           * if the end of the top allocation has shifted to a lower address
           * without a corresponding shift in the end of the writable region for
           * the heap, the total free count will be misleading.
           */
          typename VirtualAddressMap<Offset>::const_iterator itMap =
              _addressMap.find(_top);
          Offset endWritableInHeap = itMap.Limit();
          Offset endHeapRange = _heapMapIterator->first + _maxHeapSize;
          if (endWritableInHeap > endHeapRange) {
            endWritableInHeap = endHeapRange;
          }
          _allocationSize = endWritableInHeap - _allocationAddress;
        }
        _allocationIsUsed = false;
      } else {
        _allocationIsUsed = true;
      }
      _prevCheck = _check;
      _check += _chunkSize;
      return true;
    }
    return false;
  }

  Offset HandleNonMainArenaCorruption(
      const typename InfrastructureFinder<Offset>::Heap& heap,
      Offset corruptionPoint) {
    std::cerr << "Corruption was found in non-main arena run near 0x"
              << std::hex << corruptionPoint << "\n";
    Offset arenaAddress = heap._arenaAddress;
    Offset heapAddress = heap._address;
    std::cerr << "Corrupt heap is at 0x" << std::hex << heapAddress << "\n";
    std::cerr << "Corrupt arena is at 0x" << std::hex << arenaAddress << "\n";
    Offset heapLimit = heapAddress + heap._size;
    return _corruptionSkipper.SkipArenaCorruption(arenaAddress, corruptionPoint,
                                                  heapLimit);
  }
};

}  // namespace LibcMalloc
}  // namespace chap
