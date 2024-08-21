// Copyright (c) 2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace TCMalloc {
template <class Offset>
class PageMapAllocationFinder : public Allocations::Directory<Offset>::Finder {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;

  PageMapAllocationFinder(
      const VirtualAddressMap<Offset>& addressMap,
      const InfrastructureFinder<Offset>& infrastructureFinder,
      Allocations::Directory<Offset>& allocationDirectory)
      : _addressMap(addressMap),
        _spanReader(addressMap),
        _sizeReader(addressMap),
        _infrastructureFinder(infrastructureFinder),
        _allocationDirectory(allocationDirectory),
        _freeAllocationListInSpan(
            infrastructureFinder.GetFreeAllocationListInSpan()),
        _bitMapOrCacheInSpan(infrastructureFinder.GetBitMapOrCacheInSpan()),
        _cacheSizeInSpan(infrastructureFinder.GetCacheSizeInSpan()),
        _freeObjectIndexInSpan(infrastructureFinder.GetFreeObjectIndexInSpan()),
        _embedCountInSpan(infrastructureFinder.GetEmbedCountInSpan()),
        _usedObjectCountInSpan(infrastructureFinder.GetUsedObjectCountInSpan()),
        _pageOffsetBits(infrastructureFinder.GetPageOffsetBits()),
        _sizes(infrastructureFinder.GetSizes()),
        _numSizes(infrastructureFinder.GetNumSizes()),
        _pageMapIterator(_infrastructureFinder.MakePageMapIterator()) {
    _largestSmallSize =
        (Offset)_sizeReader.ReadU32(_sizes + (_numSizes - 1) * 4);
    _sizeToMinRequestSize[0] = 0;
    Offset prevSize = 0;
    for (Offset i = 1; i < _numSizes; i++) {
      Offset size = _sizeReader.ReadU32(_sizes + i * 4);
      _sizeToMinRequestSize[size] = prevSize + 1;
      prevSize = size;
    }
    if (_pageMapIterator->Finished()) {
      return;
    }
    _allocationAddress = _pageMapIterator->FirstAddressForSpan();
    _allocationSize = _pageMapIterator->AllocationSize();
    _allocationIsUsed = _pageMapIterator->SpanIsUsed();
    _indexInSpan = 0;
    _numAllocationsInSpan = _pageMapIterator->NumAllocationsInSpan();
  }

  virtual ~PageMapAllocationFinder() {}

  /*
   * Return true if there are no more allocations available.
   */
  virtual bool Finished() { return _pageMapIterator->Finished(); }

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
    if (_pageMapIterator->Finished()) {
      return;
    }

    if (++_indexInSpan < _numAllocationsInSpan) {
      _allocationAddress += _allocationSize;
      return;
    }

    _pageMapIterator->Advance();
    if (_pageMapIterator->Finished()) {
      CorrectAllocationFreeStatus();
      return;
    }

    _allocationAddress = _pageMapIterator->FirstAddressForSpan();
    _allocationSize = _pageMapIterator->AllocationSize();
    _allocationIsUsed = _pageMapIterator->SpanIsUsed();
    _indexInSpan = 0;
    _numAllocationsInSpan = _pageMapIterator->NumAllocationsInSpan();
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
      return size - (1 << _pageOffsetBits) + 1;
    }
    return size;
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  typename VirtualAddressMap<Offset>::Reader _spanReader;
  typename VirtualAddressMap<Offset>::Reader _sizeReader;
  std::unordered_map<Offset, Offset> _sizeToMinRequestSize;
  Offset _largestSmallSize;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  Allocations::Directory<Offset>& _allocationDirectory;
  const Offset _freeAllocationListInSpan;
  const Offset _bitMapOrCacheInSpan;
  const Offset _cacheSizeInSpan;
  const Offset _freeObjectIndexInSpan;
  const Offset _embedCountInSpan;
  const Offset _usedObjectCountInSpan;
  const Offset _pageOffsetBits;
  const Offset _sizes;
  const Offset _numSizes;

  std::unique_ptr<PageMapIterator<Offset> > _pageMapIterator;
  Offset _indexInSpan;
  Offset _numAllocationsInSpan;

  /*
   * The following are values for the lowest addressed allocation not yet
   * reported by the finder, possibly for a span that contains smaller
   * allocations.
   */
  Offset _allocationAddress;
  Offset _allocationSize;
  bool _allocationIsUsed;

  void CorrectAllocationFreeStatus() { CorrectCentrallyFreeAllocationStatus(); }

  void MarkAllocationRunAsFree(Offset address, Offset size,
                               Offset numAllocations, Offset span) {
    AllocationIndex index = _allocationDirectory.AllocationIndexOf(address);
    if (index == _allocationDirectory.NumAllocations()) {
      std::cerr << "Warning: Unregistered allocation at 0x" << std::hex
                << address << "in free allocation run for span at 0x" << span
                << ".\n";
      return;
    }
    for (Offset limit = address + size * numAllocations; address < limit;
         address += size) {
      if (_allocationDirectory.AllocationAt(index)->Address() != address) {
        std::cerr << "Warning: Misaligned allocation at 0x" << std::hex
                  << address << "in free allocation run for span at 0x" << span
                  << ".\n";
        return;
      }
      _allocationDirectory.MarkAsFree(index);
      index++;
    }
  }

  void MarkFreeAllocationsFromBitMap(Offset span, Offset firstAddressForSpan,
                                     Offset allocationSize,
                                     Offset addressLimitForSpan) {
    Offset bitMap = _spanReader.ReadOffset(span + _bitMapOrCacheInSpan, 0);
    if (bitMap == 0) {
      return;
    }
    AllocationIndex index =
        _allocationDirectory.AllocationIndexOf(firstAddressForSpan);
    if (index == _allocationDirectory.NumAllocations()) {
      std::cerr << "Warning: Unregistered allocation at 0x" << std::hex
                << firstAddressForSpan << "in allocation run for span at 0x"
                << span << ".\n";
      return;
    }
    for (Offset address = firstAddressForSpan; address < addressLimitForSpan;
         address += allocationSize) {
      if (bitMap == 0) {
        return;
      }
      if ((bitMap & 1) == 1) {
        _allocationDirectory.MarkAsFree(index);
      }
      bitMap >>= 1;
      index++;
    }
  }

  void MarkFreeAllocationsFromCache(Offset span, Offset firstAddressForSpan,
                                    Offset allocationSize,
                                    Offset addressLimitForSpan) {
    Offset cache =
        _spanReader.ReadOffset(span + _bitMapOrCacheInSpan, ~((Offset)0));
    AllocationIndex index =
        _allocationDirectory.AllocationIndexOf(firstAddressForSpan);
    if (index == _allocationDirectory.NumAllocations()) {
      std::cerr << "Warning: Unregistered allocation at 0x" << std::hex
                << firstAddressForSpan << "in allocation run for span at 0x"
                << span << ".\n";
      return;
    }
    unsigned char numLeftInCache =
        _spanReader.ReadU8(span + _cacheSizeInSpan, 0);
    if (numLeftInCache > 4) {
      std::cerr << "Warning: unexpected cache size for span at 0x" << std::hex
                << span << "\n";
      return;
    }
    Offset maxObjectIndex = (1 << (_pageOffsetBits - 3)) - allocationSize / 8;
    while (numLeftInCache-- != 0) {
      unsigned short objectIndex = cache & 0xffff;
      if (objectIndex > maxObjectIndex) {
        std::cerr << "Warning: unexpected entry in cache for span at 0x"
                  << std::hex << span << "\n";
        return;
      }
      _allocationDirectory.MarkAsFree(
          index + ((AllocationIndex)((objectIndex << 3) / allocationSize)));
      cache >>= 16;
    }
  }

  void MarkFreeAllocationsFromCompressedList(Offset span,
                                             Offset firstAddressForSpan,
                                             Offset allocationSize,
                                             Offset addressLimitForSpan) {
    AllocationIndex index =
        _allocationDirectory.AllocationIndexOf(firstAddressForSpan);
    if (index == _allocationDirectory.NumAllocations()) {
      std::cerr << "Warning: Unregistered allocation at 0x" << std::hex
                << firstAddressForSpan << "in allocation run for span at 0x"
                << span << ".\n";
      return;
    }
    unsigned short freeObjectIndex = _spanReader.ReadU16(
        span + _freeObjectIndexInSpan, (unsigned short)(0xffff));
    if (freeObjectIndex == (unsigned short)(0xffff)) {
      return;
    }
    Offset indexCountInFullBlock = (allocationSize / 2) - 1;
    unsigned short indexCountInBlock = _spanReader.ReadU16(
        span + _embedCountInSpan, indexCountInFullBlock + 1);
    if (indexCountInBlock > indexCountInFullBlock) {
      std::cerr << "Warning: Unexpected embed count 0x" << std::hex
                << indexCountInBlock << "in allocation run for span at 0x"
                << span << ".\n";
      return;
    }
    Offset maxObjectIndex = (1 << (_pageOffsetBits - 3)) - allocationSize / 8;
    Offset timeThroughLoop = 0;
    Offset linkAddr = 0;
    do {
      if (timeThroughLoop++ > maxObjectIndex) {
        std::cerr << "Warning: cycle in compressed free list for "
                     "span at 0x"
                  << std::hex << span << "\n";
        return;
      }
      if (freeObjectIndex > maxObjectIndex) {
        if (linkAddr == 0) {
          std::cerr << "Warning: unexpected header 0x" << std::hex
                    << freeObjectIndex
                    << " for compressed free list for span at 0x" << std::hex
                    << span << "\n";
        } else {
          std::cerr << "Warning: unexpected link index 0x" << std::hex
                    << freeObjectIndex << " referenced from link at 0x"
                    << linkAddr
                    << "\n... in compressed free list for span at 0x"
                    << std::hex << span << "\n";
        }
        return;
      }
      _allocationDirectory.MarkAsFree(
          index + ((AllocationIndex)((freeObjectIndex << 3) / allocationSize)));
      linkAddr = firstAddressForSpan + (freeObjectIndex << 3);
      for (Offset indexInBlock = 1; indexInBlock <= indexCountInBlock;
           indexInBlock++) {
        Offset objectIndexFromArray = (Offset)(_spanReader.ReadU16(
            linkAddr + indexInBlock * 2, (unsigned short)(0xffff)));
        if (objectIndexFromArray > maxObjectIndex) {
          std::cerr << "Warning: unexpected array entry in link 0x" << std::hex
                    << linkAddr << "\n... in compressed free list for "
                                   "span at 0x"
                    << std::hex << span << "\n";
          break;
        }
        _allocationDirectory.MarkAsFree(
            index +
            ((AllocationIndex)((objectIndexFromArray << 3) / allocationSize)));
      }
      indexCountInBlock = indexCountInFullBlock;
      freeObjectIndex = _spanReader.ReadU16(linkAddr, (unsigned short)(0xffff));
    } while (freeObjectIndex != (unsigned short)(0xffff));
  }

  void CorrectFreeAllocationsOnListForSpan(Offset span, Offset firstAddress,
                                           Offset addressLimit,
                                           Offset allocationSize,
                                           Offset numAllocations) {
    Offset allocationAddress =
        _spanReader.ReadOffset(span + _freeAllocationListInSpan, 0);
    Offset usedObjectCount =
        (Offset)(_spanReader.ReadU16(span + _usedObjectCountInSpan, 0));
    if (usedObjectCount == 0) {
      MarkAllocationRunAsFree(firstAddress, allocationSize, numAllocations,
                              span);
      return;
    }
    if (usedObjectCount > numAllocations) {
      std::cerr << "Warning: The span at 0x" << std::hex << span
                << " has used object count " << std::dec << usedObjectCount
                << " but only " << numAllocations << " for the whole span.\n";
      return;
    }

    Offset numAllocationsOnList = 0;
    Offset numFreeAllocationsExpected = numAllocations - usedObjectCount;
    AllocationIndex numAllocationsInDirectorySoFar =
        _allocationDirectory.NumAllocations();
    for (; allocationAddress != 0;
         allocationAddress = _spanReader.ReadOffset(allocationAddress, 0)) {
      if (++numAllocationsOnList > numFreeAllocationsExpected + 10) {
        break;
      }

      if (allocationAddress < firstAddress ||
          (allocationAddress + allocationSize) > addressLimit) {
        std::cerr << "Warning: Unexpected allocation at 0x" << std::hex
                  << allocationAddress
                  << "in free allocation list for span at 0x" << span << ".\n";
        return;
      }
      AllocationIndex index =
          _allocationDirectory.AllocationIndexOf(allocationAddress);
      if (index == numAllocationsInDirectorySoFar) {
        std::cerr << "Warning: Unregistered allocation at 0x" << std::hex
                  << allocationAddress
                  << "in free allocation list for span at 0x" << span << ".\n";
        return;
      }
      if (_allocationDirectory.AllocationAt(index)->Address() !=
          allocationAddress) {
        std::cerr << "Warning: Misaligned allocation at 0x" << std::hex
                  << allocationAddress
                  << "in free allocation list for span at 0x" << span << ".\n";
        return;
      }
      _allocationDirectory.MarkAsFree(index);
    }
    if (numAllocationsOnList != numFreeAllocationsExpected) {
      std::cerr << "For span 0x" << std::hex << span << ", " << std::dec
                << numAllocationsOnList << " allocations were found but "
                << numFreeAllocationsExpected << " were expected.\n";
    }
  }

  void CorrectCentrallyFreeAllocationStatus() {
    std::unique_ptr<PageMapIterator<Offset> > pageMapIterator(
        _infrastructureFinder.MakePageMapIterator());
    for (; !(pageMapIterator->Finished()); pageMapIterator->Advance()) {
      if (!(pageMapIterator->SpanIsUsed())) {
        continue;
      }
      Offset allocationSize = pageMapIterator->AllocationSize();
      Offset spanSize = pageMapIterator->SpanSize();
      if (allocationSize == spanSize) {
        continue;
      }
      Offset span = pageMapIterator->Span();
      Offset numAllocationsInSpan = pageMapIterator->NumAllocationsInSpan();
      Offset firstAddressForSpan = pageMapIterator->FirstAddressForSpan();
      Offset addressLimitForSpan = firstAddressForSpan + spanSize;

      if (_freeAllocationListInSpan !=
          InfrastructureFinder<Offset>::NOT_A_FIELD_OFFSET) {
        CorrectFreeAllocationsOnListForSpan(span, firstAddressForSpan,
                                            addressLimitForSpan, allocationSize,
                                            numAllocationsInSpan);
        continue;
      }

      if (_bitMapOrCacheInSpan !=
          InfrastructureFinder<Offset>::NOT_A_FIELD_OFFSET) {
        if (numAllocationsInSpan <= (sizeof(Offset) * 8)) {
          MarkFreeAllocationsFromBitMap(span, firstAddressForSpan,
                                        allocationSize, addressLimitForSpan);
        } else {
          MarkFreeAllocationsFromCache(span, firstAddressForSpan,
                                       allocationSize, addressLimitForSpan);
          MarkFreeAllocationsFromCompressedList(
              span, firstAddressForSpan, allocationSize, addressLimitForSpan);
        }
        continue;
      }
    }
  }
};

}  // namespace TCMalloc
}  // namespace chap
