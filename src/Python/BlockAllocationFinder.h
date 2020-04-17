// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace Python {
template <class Offset>
class BlockAllocationFinder : public Allocations::Directory<Offset>::Finder {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;

  BlockAllocationFinder(
      const VirtualAddressMap<Offset>& addressMap,
      const InfrastructureFinder<Offset>& infrastructureFinder)
      : _addressMap(addressMap),
        _reader(addressMap),
        _infrastructureFinder(infrastructureFinder),
        _arenaStructArray(infrastructureFinder.ArenaStructArray()),
        _arenaStructSize(infrastructureFinder.ArenaStructSize()),
        _arenaSize(infrastructureFinder.ArenaSize()),
        _poolSize(infrastructureFinder.PoolSize()),
        _activeIndices(infrastructureFinder.ActiveIndices()),
        _itActiveIndices(_activeIndices.begin()) {
    if (_itActiveIndices != _activeIndices.end()) {
      Offset maxBlocksInPool = (_poolSize - 0x30) / sizeof(Offset);
      _blockUsedInPool.reserve(maxBlocksInPool);
      _blockUsedInPool.resize(maxBlocksInPool, true);
      _arena = _reader.ReadOffset(_arenaStructArray +
                                  _arenaStructSize * (*_itActiveIndices));
      while (!AdvanceToFirstAllocationOfArena()) {
        if (++_itActiveIndices == _activeIndices.end()) {
          break;
        }
      }
    }
  }

  virtual ~BlockAllocationFinder() {}

  /*
   * Return true if there are no more allocations available.
   */
  virtual bool Finished() { return _itActiveIndices == _activeIndices.end(); }

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
    if (_itActiveIndices != _activeIndices.end()) {
      if (!AdvanceToNextAllocationOfArena()) {
        do {
          if (++_itActiveIndices == _activeIndices.end()) {
            // ?? any finalization needed now that all are numbered
            return;
          }
          _arena = _reader.ReadOffset(_arenaStructArray +
                                      _arenaStructSize * (*_itActiveIndices));
        } while (!AdvanceToFirstAllocationOfArena());
      }
    }
  }
  /*
   * Return the smallest request size that might reasonably have resulted
   * in an allocation of the given size.
   */
  virtual Offset MinRequestSize(Offset size) {
    return size - sizeof(Offset) + 1;
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  typename VirtualAddressMap<Offset>::Reader _reader;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const Offset _arenaStructArray;
  const Offset _arenaStructSize;
  const Offset _arenaSize;
  const Offset _poolSize;
  const std::vector<uint32_t>& _activeIndices;
  std::vector<uint32_t>::const_iterator _itActiveIndices;
  std::vector<bool> _blockUsedInPool;

  Offset _arena;
  Offset _firstPool;
  Offset _poolsLimit;
  Offset _pool;
  Offset _blockSize;
  Offset _block;
  size_t _blockIndex;
  Offset _blocksLimit;
  Offset _allocationAddress;
  Offset _allocationSize;
  bool _allocationIsUsed;

  bool AdvanceToFirstAllocationOfArena() {
    _firstPool = (_arena + (_poolSize - 1)) & ~(_poolSize - 1);
    _poolsLimit = (_arena + _arenaSize) & ~(_poolSize - 1);
    for (_pool = _firstPool; _pool < _poolsLimit; _pool += _poolSize) {
      if (AdvanceToFirstAllocationOfPool()) {
        return true;
      }
    }
    return false;
  }
  bool AdvanceToFirstAllocationOfPool() {
    if (_reader.ReadU32(_pool, 0) == 0) {
      return false;
    }
    _blockSize = _poolSize - ((Offset)(_reader.ReadU32(_pool + 0x2c, 0)));
    if (_blockSize == _poolSize) {
      return false;
    }
    _blockIndex = 0;
    _block = _pool + 0x30;
    Offset numBlocks = (_poolSize - 0x30) / _blockSize;
    _blocksLimit = _block + (_blockSize * numBlocks);
    Offset nextInPool = (Offset)(_reader.ReadU32(
        _pool + ((4 * sizeof(Offset)) + (2 * sizeof(uint32_t))), 0));
    Offset numBlocksEverUsed = (nextInPool - 0x30) / _blockSize;
    if (nextInPool < 0x30 || numBlocksEverUsed > numBlocks ||
        nextInPool != (0x30 + numBlocksEverUsed * _blockSize)) {
      std::cerr
          << "Warning: Probable corruption in header for python pool at 0x"
          << std::hex << _pool << "\n";
      /*
       * In such a case, act as if the pool is all used, because we need
       * to pick some value in range and the check at the start of the
       * function makes it very unlikely that all blocks are freed.  This
       * is slightly questionable at present because of the possibility of
       * 0-filled pages in the case of an incomplete core.
       */
      numBlocksEverUsed = numBlocks;
    }
    for (Offset i = 0; i < numBlocksEverUsed; i++) {
      _blockUsedInPool[i] = true;
    }
    for (size_t i = numBlocksEverUsed; i < numBlocks; i++) {
      _blockUsedInPool[i] = false;
    }
    for (Offset freeBlock = _reader.ReadOffset(_pool + 8, 0); freeBlock != 0;
         freeBlock = _reader.ReadOffset(freeBlock, 0)) {
      if (freeBlock < _block || freeBlock >= _blocksLimit) {
        std::cerr << "Warning: probable corrupt free list found for pool at 0x"
                  << std::hex << _pool
                  << ".\nFree status cannot be trusted for this pool.\n";
        break;
      }
      _blockUsedInPool[(freeBlock - _block) / _blockSize] = false;
    }
    _allocationAddress = _block;
    _allocationSize = _blockSize;
    _allocationIsUsed = _blockUsedInPool[0];
    return true;
  }
  bool AdvanceToNextAllocationOfArena() {
    _block += _blockSize;
    ++_blockIndex;
    if (_block < _blocksLimit) {
      _allocationAddress = _block;
      _allocationSize = _blockSize;
      _allocationIsUsed = _blockUsedInPool[_blockIndex];
      return true;
    }
    for (_pool += _poolSize; _pool < _poolsLimit; _pool += _poolSize) {
      if (AdvanceToFirstAllocationOfPool()) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace LibcMalloc
}  // namespace chap
