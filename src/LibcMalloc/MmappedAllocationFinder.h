// Copyright (c) 2017-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"

namespace chap {
namespace LibcMalloc {
template <class Offset>
class MmappedAllocationFinder : public Allocations::Directory<Offset>::Finder {
 public:
  MmappedAllocationFinder(
      VirtualMemoryPartition<Offset>& virtualMemoryPartition)
      : LIBC_MALLOC_MMAPPED_ALLOCATION("libc malloc mmapped allocation"),
        _virtualMemoryPartition(virtualMemoryPartition),
        _addressMap(virtualMemoryPartition.GetAddressMap()) {
    ScanForMmappedChunks();
    _itNext = _mmappedChunks.begin();
  }

  /*
   * Return true if there are no more allocations available.
   */
  virtual bool Finished() { return _itNext == _mmappedChunks.end(); }
  /*
   * Return the address of the next allocation (in increasing order of
   * address) to be reported by this finder, without advancing to the next
   * allocation.  The return value is undefined if there are no more
   * allocations available.  Note that at the time this function is called
   * any allocations already reported by this allocation finder have already
   * been assigned allocation indices in the Directory.
   */
  virtual Offset NextAddress() { return _itNext->first + 2 * sizeof(Offset); }
  /*
   * Return the size of the next allocation (in increasing order of
   * address) to be reported by this finder, without advancing to the next
   * allocation.  The return value is undefined if there are no more
   * allocations available.
   */
  virtual Offset NextSize() { return _itNext->second - 2 * sizeof(Offset); }
  /*
   * Return true if the next allocation (in increasing order of address) to
   * address) to be reported by this finder is considered used, without
   * advancing to the next allocation.
   */
  virtual bool NextIsUsed() { return true; }
  /*
   * Advance to the next allocation.
   */
  virtual void Advance() {
    if (_itNext != _mmappedChunks.end()) {
      ++_itNext;
    }
  }
  /*
   * Return the smallest request size that might reasonably have resulted
   * in an allocation of the given size.
   */
  virtual Offset MinRequestSize(Offset size) { return size - 0x1000 + 1; }

  const std::map<Offset, Offset>& GetMmappedChunks() const {
    return _mmappedChunks;
  }

 private:
  const char* LIBC_MALLOC_MMAPPED_ALLOCATION;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _addressMap;
  std::map<Offset, Offset> _mmappedChunks;  // start -> size
  typename std::map<Offset, Offset>::const_iterator _itNext;

  void ScanForMmappedChunksInRange(Offset base, Offset limit) {
    typename VirtualAddressMap<Offset>::Reader reader(_addressMap);
    Offset candidate = (base + 0xFFF) & ~0xFFF;
    while (candidate <= limit - 0x1000) {
      Offset expect0 = reader.ReadOffset(candidate, 0xbadbad);
      Offset chunkSizeAndFlags =
          reader.ReadOffset(candidate + sizeof(Offset), 0xbadbad);
      bool foundMmappedAlloc =
          (expect0 == 0) &&
          ((chunkSizeAndFlags & ((Offset)0xFFF)) == ((Offset)2)) &&
          (chunkSizeAndFlags >= ((Offset)0x1000)) &&
          (candidate + chunkSizeAndFlags - 2) > candidate &&
          (candidate + chunkSizeAndFlags - 2) <= limit;
      if (!foundMmappedAlloc) {
        candidate += 0x1000;
      } else {
        Offset chunkSize = chunkSizeAndFlags - 2;

        _mmappedChunks[candidate] = chunkSize;
        candidate += chunkSize;
      }
    }
  }

  void ScanForMmappedChunks() {
    for (const auto& range :
         _virtualMemoryPartition.GetUnclaimedWritableRangesWithImages()) {
      ScanForMmappedChunksInRange(range._base, range._limit);
    }
    for (const auto& addressAndSize : _mmappedChunks) {
      if (!_virtualMemoryPartition.ClaimRange(
              addressAndSize.first, addressAndSize.second,
              LIBC_MALLOC_MMAPPED_ALLOCATION, false)) {
        std::cerr << "Warning: unexpected overlap for mmapped allocation at 0x"
                  << std::hex << addressAndSize.first << "\n";
      }
    }
  }
};

}  // namespace LibcMalloc
}  // namespace chap
