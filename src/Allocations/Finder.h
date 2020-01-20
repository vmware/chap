// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <set>
#include "../VirtualAddressMap.h"
namespace chap {
namespace Allocations {
template <class Offset>
class Finder {
 public:
  typedef unsigned int AllocationIndex;
  class Allocation {
   public:
    static const Offset SIZE_MASK = (~((Offset)0)) >> 1;
    Allocation(Offset address, Offset size, bool isAllocated)
        : _address(address),
          _sizeAndFlag(size | (isAllocated ? ~SIZE_MASK : 0)) {}
    void MarkAsFree() {
      _sizeAndFlag &= SIZE_MASK;  // Clear the bit that indicates use
    }
    Offset Address() const { return _address; }
    Offset Size() const { return _sizeAndFlag & SIZE_MASK; }
    bool IsUsed() const { return (_sizeAndFlag & ~SIZE_MASK) != 0; }
    // A Visitor returns true in the case that traversal should stop.
    typedef std::function<bool(Offset,        // allocation address
                               Offset,        // allocation size
                               bool,          // is allocated
                               const char*)>  // allocation image
        Visitor;
    // A Checker returns true in the case that the allocation should be
    // visited.
    typedef std::function<bool(Offset,        // allocation address
                               Offset,        // allocation size
                               bool,          // is allocated
                               const char*)>  // allocation image
        Checker;
    void AdjustSize(Offset newSize) {
      // TODO: remove this, possibly.  It is needed only if the size
      // value has been trashed in memory.
      _sizeAndFlag = newSize | (_sizeAndFlag & ~SIZE_MASK);
    }
    void AdjustAddress(Offset address) { _address = address; }

   private:
    Offset _address;
    Offset _sizeAndFlag;
  };
  struct CompareAllocations {
    bool operator()(const Allocation& left, const Allocation& right) {
      return left.Address() < right.Address();
    }
  };

  Finder(const VirtualAddressMap<Offset>& addressMap)
      : _addressMap(addressMap) {}

  virtual ~Finder() {}
  // index is same as NumAllocations() if offset is not in any range.
  virtual AllocationIndex AllocationIndexOf(Offset addr) const = 0;
  // null if index is not valid.
  virtual const Allocation* AllocationAt(AllocationIndex index) const = 0;
  // 0 If index is not valid
  virtual Offset MinRequestSize(AllocationIndex index) const = 0;
  virtual AllocationIndex NumAllocations() const = 0;
  virtual Offset MaxAllocationSize() const = 0;
  // Return target index or NumAllocations() if none exists or edge is
  // not suitable as an anchor.
  virtual AllocationIndex EdgeTargetIndex(Offset targetCandidate) const = 0;
  const VirtualAddressMap<Offset>& GetAddressMap() const { return _addressMap; }
  virtual bool HasThreadCached() const { return false; }
  virtual bool IsThreadCached(AllocationIndex) const { return false; }

 protected:
  const VirtualAddressMap<Offset>& _addressMap;
};
}  // namespace Allocations
}  // namespace chap
