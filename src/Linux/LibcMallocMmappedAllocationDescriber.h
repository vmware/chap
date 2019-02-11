// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Describer.h"
#include "../ProcessImage.h"
#include "LibcMallocAllocationFinder.h"

namespace chap {
namespace Linux {
template <typename Offset>
class LibcMallocMmappedAllocationDescriber : public Describer<Offset> {
 public:
  typedef typename LibcMallocAllocationFinder<Offset>::Heap Heap;
  typedef typename LibcMallocAllocationFinder<Offset>::HeapMap HeapMap;
  typedef typename LibcMallocAllocationFinder<Offset>::HeapMapConstIterator
      HeapMapConstIterator;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  LibcMallocMmappedAllocationDescriber(
      const LibcMallocAllocationFinder<Offset> *finder)
      : _mmappedAllocations((finder != nullptr)
                                ? &(finder->GetMmappedAllocations())
                                : nullptr) {}
  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context &context, Offset address, bool explain,
                bool showAddresses) const {
    if (_mmappedAllocations == nullptr) {
      return false;
    }
    typename LibcMallocAllocationFinder<Offset>::MmappedAllocationsConstIterator
        it = _mmappedAllocations->upper_bound(address);

    if (it == _mmappedAllocations->begin()) {
      return false;
    }

    --it;
    Offset mmappedRangeStart = it->first;
    if (address < mmappedRangeStart) {
      return false;
    }
    Offset mmappedRangeLimit = mmappedRangeStart + it->second;
    if (address >= mmappedRangeLimit) {
      return false;
    }

    Offset offsetInAllocation = address - mmappedRangeStart;
    Commands::Output &output = context.GetOutput();
    if (showAddresses) {
      output << "Address 0x" << std::hex << address << " is at offset 0x"
             << offsetInAllocation
             << " of the individually mmapped chunk\nat [0x"
             << mmappedRangeStart << ", 0x" << mmappedRangeLimit << ").\n";
      if (offsetInAllocation < sizeof(Offset)) {
        output << "It is in the prev size field for the libc chunk for the "
                  "allocation\nat 0x"
               << (mmappedRangeStart + 2 * sizeof(Offset)) << ".\n";
      } else {
        /*
         * Note that we expect the describer for allocations to describe any
         * address in an allocation, including what libc would consider to be
         * the prev size for a libc chunk on the doubly linked free list. For
         * this reason, this describer only mentions the prev size entry for the
         * first allocation.
         */
        output << "It is in the size/status field for the libc chunk for the "
                  "allocation\nat 0x"
               << (mmappedRangeStart + 2 * sizeof(Offset)) << ".\n";
      }
    } else {
      output << "This is an individually mmapped libc chunk for a single "
                "allocation.\n";
    }
    if (explain) {
    }
    return true;
  }

 private:
  const typename LibcMallocAllocationFinder<Offset>::MmappedAllocations
      *_mmappedAllocations;
};
}  // namespace Linux
}  // namespace chap
