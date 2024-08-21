// Copyright (c) 2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace GoLang {
template <class Offset>
class MappedPageRangeAllocationFinder
    : public Allocations::Directory<Offset>::Finder {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;

  MappedPageRangeAllocationFinder(
      const VirtualAddressMap<Offset>& addressMap,
      const InfrastructureFinder<Offset>& infrastructureFinder,
      Allocations::Directory<Offset>& allocationDirectory)
      : _addressMap(addressMap),
        _mspanReader(addressMap),
        _sizeReader(addressMap),
        _allocBitsReader(addressMap),
        _infrastructureFinder(infrastructureFinder),
        _allocationDirectory(allocationDirectory),
        _numElementsInMspan(infrastructureFinder.GetNumElementsInMspan()),
        _elementSizeInMspan(infrastructureFinder.GetElementSizeInMspan()),
        _allocBitsInMspan(infrastructureFinder.GetAllocBitsInMspan()),
        _manualFreeListInMspan(infrastructureFinder.GetManualFreeListInMspan()),
        _stateInMspan(infrastructureFinder.GetStateInMspan()),
        _pageOffsetBits(infrastructureFinder.GetPageOffsetBits()),
        _pageSize(1 << _pageOffsetBits),
        _sizes(infrastructureFinder.GetSizes()),
        _numSizes(infrastructureFinder.GetNumSizes()),
        _rangeIterator(_infrastructureFinder.MakeMappedPageRangeIterator()) {
    _largestSmallSize =
        (Offset)_sizeReader.ReadU32(_sizes + (_numSizes - 1) * 4);
    _sizeToMinRequestSize[0] = 0;
    Offset prevSize = 0;
    for (Offset i = 1; i < _numSizes; i++) {
      Offset size = _sizeReader.ReadU32(_sizes + i * 4);
      _sizeToMinRequestSize[size] = prevSize + 1;
      prevSize = size;
    }
    if (_rangeIterator->Finished()) {
      return;
    }
    SetFirstAllocationFromIterator();
  }

  virtual ~MappedPageRangeAllocationFinder() {}

  /*
   * Return true if there are no more allocations available.
   */
  virtual bool Finished() { return _rangeIterator->Finished(); }

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
    if (_rangeIterator->Finished()) {
      return;
    }

    if (++_indexInRange < _numAllocationsInRange) {
      _allocationAddress += _allocationSize;
      if (_allocBits != 0) {
        _allocationIsUsed =
            ((_allocBitsReader.ReadU8(_allocBits + _indexInRange / 8, 0) &
              (1 << (_indexInRange & 7))) != 0);
      }
      return;
    }

    _rangeIterator->Advance();
    if (_rangeIterator->Finished()) {
      CorrectAllocationFreeStatus();
      return;
    }

    SetFirstAllocationFromIterator();
  }

  /*
   * Return the smallest request size that might reasonably have resulted
   * in an allocation of the given size.
   */
  virtual Offset MinRequestSize(Offset size) {
    auto it = _sizeToMinRequestSize.find(size);
    if (it != _sizeToMinRequestSize.end()) {
      return it->second;
    }
    if (size > _largestSmallSize) {
      return size - _pageSize + 1;
    }
    return size;
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  typename VirtualAddressMap<Offset>::Reader _mspanReader;
  typename VirtualAddressMap<Offset>::Reader _sizeReader;
  typename VirtualAddressMap<Offset>::Reader _allocBitsReader;
  std::unordered_map<Offset, Offset> _sizeToMinRequestSize;
  Offset _largestSmallSize;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  Allocations::Directory<Offset>& _allocationDirectory;
  const Offset _numElementsInMspan;
  const Offset _elementSizeInMspan;
  const Offset _allocBitsInMspan;
  const Offset _manualFreeListInMspan;
  const Offset _stateInMspan;
  const Offset _pageOffsetBits;
  const Offset _pageSize;
  const Offset _sizes;
  const Offset _numSizes;

  std::unique_ptr<MappedPageRangeIterator<Offset> > _rangeIterator;
  Offset _indexInRange;
  Offset _numAllocationsInRange;
  Offset _allocBits;

  /*
   * The following are values for the lowest addressed allocation not yet
   * reported by the finder, possibly for a span that contains smaller
   * allocations.
   */
  Offset _allocationAddress;
  Offset _allocationSize;
  bool _allocationIsUsed;

  void SetFirstAllocationFromIterator() {
    _allocationAddress = _rangeIterator->FirstAddressForRange();
    Offset rangeSize = _rangeIterator->RangeSize();
    _allocationSize = rangeSize;
    _indexInRange = 0;
    _numAllocationsInRange = 1;
    _allocationIsUsed = false;
    _allocBits = 0;
    Offset mspan = _rangeIterator->Mspan();
    if (mspan != 0) {
      unsigned char state = _mspanReader.ReadU8(mspan + _stateInMspan, 0);
      if (state == 1 || state == 2) {
        Offset elementSize =
            _mspanReader.ReadOffset(mspan + _elementSizeInMspan, 0);
        if (state == 1) {
          _allocBits = _mspanReader.ReadOffset(mspan + _allocBitsInMspan, 0);
          _allocationIsUsed =
              ((_allocBitsReader.ReadU8(_allocBits, 0) & 1) != 0);
          Offset numElementsInRange =
              _mspanReader.ReadU16(mspan + _numElementsInMspan, 0);
          if (numElementsInRange != 0 && elementSize != 0 &&
              (elementSize * numElementsInRange) <= rangeSize) {
            _numAllocationsInRange = numElementsInRange;
            _allocationSize = elementSize;
          }
        } else {
          // The used/free status is fixed later, based on the manual
          // free list if it isn't empty.
          _allocationIsUsed = true;
          if ((elementSize != 0) && (elementSize <= rangeSize)) {
            _numAllocationsInRange = rangeSize / elementSize;
            _allocationSize = elementSize;
          }
        }
      }
    }
  }

  void CorrectAllocationFreeStatus() { CorrectCentrallyFreeAllocationStatus(); }


  void CorrectCentrallyFreeAllocationStatus() {
    if (_manualFreeListInMspan ==
        InfrastructureFinder<Offset>::NOT_A_FIELD_OFFSET) {
      return;
    }
    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    Reader manualFreeListReader(_addressMap);
    for (iterator.reset(_infrastructureFinder.MakeMappedPageRangeIterator());
         !(iterator->Finished()); iterator->Advance()) {
      Offset mspan = iterator->Mspan();
      if (mspan == 0) {
        continue;
      }
      if (_mspanReader.ReadU8(mspan + _stateInMspan, 0) != 2) {
        continue;
      }
      Offset rangeSize = iterator->RangeSize();
      Offset elementSize =
          _mspanReader.ReadOffset(mspan + _elementSizeInMspan, 0);
      if (elementSize == 0 || elementSize > rangeSize) {
        continue;
      }
      Offset manualFreeListEntry =
          _mspanReader.ReadOffset(mspan + _manualFreeListInMspan, 0);
      if (manualFreeListEntry == 0) {
        continue;
      }
      Offset firstAddress = iterator->FirstAddressForRange();
      Offset limit = firstAddress + rangeSize;
      Offset numElements = rangeSize / elementSize;
      AllocationIndex firstIndex =
          _allocationDirectory.AllocationIndexOf(firstAddress);
      Offset numAllocationsMarkedFree = 0;
      while (manualFreeListEntry != 0) {
        if (manualFreeListEntry < firstAddress || manualFreeListEntry > limit) {
          std::cerr << "Warning: mspan 0x" << mspan
                    << " has a corrupt manual free list.\n";
          break;
        }
        Offset relativeIndex =
            (manualFreeListEntry - firstAddress) / elementSize;
        if (manualFreeListEntry != firstAddress + relativeIndex * elementSize) {
          std::cerr << "Warning: mspan 0x" << mspan
                    << " has a misaligned element in the manual free list.\n";
          break;
        }
        _allocationDirectory.MarkAsFree(firstIndex + relativeIndex);
        if (++numAllocationsMarkedFree > numElements) {
          std::cerr << "Warning: mspan 0x" << mspan
                    << " has a cycle in the manual free list.\n";
          break;
        }
        manualFreeListEntry =
            manualFreeListReader.ReadOffset(manualFreeListEntry, 0);
      }
    }
  }
};

}  // namespace GoLang
}  // namespace chap
