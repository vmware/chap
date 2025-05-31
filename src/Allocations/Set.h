// Copyright (c) 2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include <memory>
#include "Directory.h"
namespace chap {
namespace Allocations {
template <class Offset>
class Set {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  Set(AllocationIndex numAllocations)
      : _numAllocations(numAllocations),
        _numU64((numAllocations + (AllocationIndex)(63)) /
                (AllocationIndex)(64)),
        _numU8(_numU64 * 8),
        _asU64(new uint64_t[_numU64]) {
    Clear();
  }
  void Clear() { memset(_asU64.get(), 0, _numU8); }
  void Add(AllocationIndex index) {
    _asU64[index / (AllocationIndex)(64)] |=
        ((uint64_t)(1) << (((uint64_t)(index) & ((uint64_t)(63)))));
  }
  bool Has(AllocationIndex index) const {
    return (_asU64[index / (AllocationIndex)(64)] &
            ((uint64_t)(1) << (((uint64_t)(index) & ((uint64_t)(63)))))) != 0;
  }

  AllocationIndex NextUsed(AllocationIndex startFrom) const {
    AllocationIndex retVal = _numAllocations;
    AllocationIndex u64Index = startFrom / (AllocationIndex)(64);
    uint64_t u64 =
        _asU64[u64Index] >> (((uint64_t)(startFrom) & ((uint64_t)(63))));
    if (u64 != 0) {
      retVal = startFrom;
    } else {
      do {
        if (++u64Index == _numU64) {
          return retVal;
        }
        u64 = _asU64[u64Index];
      } while (u64 == 0);
      retVal = u64Index * (AllocationIndex)(64);
    }
    while ((u64 & (uint64_t)(1)) == 0) {
      u64 = u64 >> 1;
      retVal++;
    }
    return retVal;
  }

  void Assign(const Set<Offset> &other) {
    uint64_t *to = _asU64.get();
    const uint64_t *from = other._asU64.get();
    for (AllocationIndex i = 0; i < _numU64; i++) {
      *to++ = *from++;
    }
  }
  void Add(const Set<Offset> &other) {
    uint64_t *to = _asU64.get();
    const uint64_t *from = other._asU64.get();
    for (AllocationIndex i = 0; i < _numU64; i++) {
      *to++ |= *from++;
    }
  }
  void Subtract(const Set<Offset> &other) {
    uint64_t *to = _asU64.get();
    const uint64_t *from = other._asU64.get();
    for (AllocationIndex i = 0; i < _numU64; i++) {
      *to++ &= ~(*from++);
    }
  }

 private:
  AllocationIndex _numAllocations;
  AllocationIndex _numU64;
  AllocationIndex _numU8;

  std::unique_ptr<uint64_t[]> _asU64;
};
}  // namespace Allocations
}  // namespace chap
