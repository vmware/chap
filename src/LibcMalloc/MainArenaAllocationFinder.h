// Copyright (c) 2017-2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../VirtualAddressMap.h"
#include "CorruptionSkipper.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace LibcMalloc {
template <class Offset>
class MainArenaAllocationFinder
    : public Allocations::Directory<Offset>::Finder {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;

  MainArenaAllocationFinder(
      const VirtualAddressMap<Offset>& addressMap,
      const InfrastructureFinder<Offset>& infrastructureFinder,
      CorruptionSkipper<Offset>& corruptionSkipper,
      FastBinFreeStatusFixer<Offset>& fastBinFreeStatusFixer,
      DoublyLinkedListCorruptionChecker<Offset>&
          doublyLinkedListCorruptionChecker,
      Allocations::Directory<Offset>& allocationDirectory)
      : _addressMap(addressMap),
        _reader(addressMap),
        _infrastructureFinder(infrastructureFinder),
        _mainArenaAddress(_infrastructureFinder.GetMainArenaAddress()),
        _mainArena(
            _infrastructureFinder.GetArenas().find(_mainArenaAddress)->second),
        _mainArenaRuns(_infrastructureFinder.GetMainArenaRuns()),
        _mainArenaRunsIterator(_mainArenaRuns.begin()),
        _corruptionSkipper(corruptionSkipper),
        _fastBinFreeStatusFixer(fastBinFreeStatusFixer),
        _doublyLinkedListCorruptionChecker(doublyLinkedListCorruptionChecker) {
    if (_mainArenaRunsIterator != _mainArenaRuns.end()) {
      StartMainArenaRun();
      Advance();
    }
    _finderIndex = allocationDirectory.AddFinder(this);
  }

  virtual ~MainArenaAllocationFinder() {}

  /*
   * Return true if there are no more allocations available.
   */
  virtual bool Finished() {
    return _mainArenaRunsIterator == _mainArenaRuns.end();
  }

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
    if (_mainArenaRunsIterator != _mainArenaRuns.end()) {
      while (!AdvanceToNextAllocationOfRun()) {
        if (++_mainArenaRunsIterator == _mainArenaRuns.end()) {
          _fastBinFreeStatusFixer.MarkFastBinItemsAsFree(_mainArena, true,
                                                         _finderIndex);
          _doublyLinkedListCorruptionChecker.CheckDoublyLinkedListCorruption(
              _mainArena);
          return;
        } else {
          StartMainArenaRun();
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
  const Offset _mainArenaAddress;
  const typename InfrastructureFinder<Offset>::Arena& _mainArena;
  const typename InfrastructureFinder<Offset>::MainArenaRuns& _mainArenaRuns;
  typename InfrastructureFinder<Offset>::MainArenaRuns::const_iterator
      _mainArenaRunsIterator;
  Offset _allocationAddress;
  Offset _allocationSize;
  bool _allocationIsUsed;
  Offset _base;
  Offset _size;
  Offset _limit;
  Offset _chunkSize;
  Offset _prevCheck;
  Offset _check;
  Offset _checkLimit;
  Offset _sizeAndFlags;
  CorruptionSkipper<Offset>& _corruptionSkipper;
  FastBinFreeStatusFixer<Offset>& _fastBinFreeStatusFixer;
  DoublyLinkedListCorruptionChecker<Offset>& _doublyLinkedListCorruptionChecker;
  size_t _finderIndex;
  void StartMainArenaRun() {
    _base = _mainArenaRunsIterator->first;
    _size = _mainArenaRunsIterator->second;
    _limit = _base + _size;
    _sizeAndFlags = _reader.ReadOffset(_base + sizeof(Offset));
    _chunkSize = 0;
    _prevCheck = _base;
    _check = _base;
  }

  bool AdvanceToNextAllocationOfRun() {
    while (_check < _limit) {
      if ((_sizeAndFlags & (sizeof(Offset) | 6)) != 0) {
        _check = HandleMainArenaCorruption(_prevCheck, _limit);
        if (_check != 0) {
          _chunkSize = 0;
          _prevCheck = _check;
          _sizeAndFlags = _reader.ReadOffset(_check + sizeof(Offset));
          continue;
        }
        break;
      }
      _chunkSize = _sizeAndFlags & ~7;

      if ((_chunkSize == 0) || (_chunkSize > (_limit - _check))) {
        _check = HandleMainArenaCorruption(_prevCheck, _limit);
        if (_check != 0) {
          _chunkSize = 0;
          _prevCheck = _check;
          _sizeAndFlags = _reader.ReadOffset(_check + sizeof(Offset));
          continue;
        }
        break;
      }
      _allocationAddress = _check + 2 * sizeof(Offset);
      _allocationSize = _chunkSize - sizeof(Offset);
      _allocationIsUsed = false;
      if (_check + _chunkSize == _limit) {
        _allocationSize -= sizeof(Offset);
      } else {
        _sizeAndFlags =
            _reader.ReadOffset(_check + sizeof(Offset) + _chunkSize);
        _allocationIsUsed = ((_sizeAndFlags & 1) != 0);
      }
      _prevCheck = _check;
      _check += _chunkSize;
      return true;
    }
    return false;
  }

  Offset HandleMainArenaCorruption(Offset corruptionPoint, Offset limit) {
    std::cerr << "Corruption was found in main arena run near 0x" << std::hex
              << corruptionPoint << "\n";
    std::cerr << "The main arena is at 0x" << std::hex << _mainArenaAddress
              << "\n";
    return _corruptionSkipper.SkipArenaCorruption(_mainArenaAddress,
                                                  corruptionPoint, limit);
  }
};

}  // namespace LibcMalloc
}  // namespace chap
