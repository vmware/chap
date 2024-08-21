// Copyright (c) 2019-2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Describer.h"
#include "../ProcessImage.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace LibcMalloc {
template <typename Offset>
class MainArenaRunDescriber : public Describer<Offset> {
 public:
  typedef typename InfrastructureFinder<Offset>::Heap Heap;
  typedef typename InfrastructureFinder<Offset>::HeapMap HeapMap;
  typedef typename InfrastructureFinder<Offset>::HeapMapConstIterator
      HeapMapConstIterator;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  MainArenaRunDescriber(
      const InfrastructureFinder<Offset> &infrastructureFinder)
      : _mainArenaRuns(infrastructureFinder.GetMainArenaRuns()) {}

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */

  bool Describe(Commands::Context &context, Offset address, bool explain,
                bool showAddresses) const {
    typename InfrastructureFinder<Offset>::MainArenaRunsConstIterator it =
        _mainArenaRuns.upper_bound(address);

    if (it == _mainArenaRuns.begin()) {
      return false;
    }

    --it;
    Offset runStart = it->first;
    if (address < runStart) {
      return false;
    }
    Offset runLimit = runStart + it->second;
    if (address >= runLimit) {
      return false;
    }

    Offset offsetInRun = address - runStart;
    Commands::Output &output = context.GetOutput();
    if (showAddresses) {
      output << "Address 0x" << std::hex << address << " is at offset 0x"
             << offsetInRun << " of the main arena allocation run\nat [0x"
             << runStart << ", 0x" << runLimit << ").\n";
      if (offsetInRun < sizeof(Offset)) {
        output << "It is in the prev size field for the libc chunk for the "
                  "first allocation\nin the allocation run.\n";
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
               << ((address + sizeof(Offset)) & ~(sizeof(Offset) - 1)) << ".\n";
      }
    } else {
      output << "This is a run of pages used for allocations for the main "
                "arena.\n";
    }
    if (explain) {
    }
    return true;
  }

 private:
  const typename InfrastructureFinder<Offset>::MainArenaRuns &_mainArenaRuns;
};
}  // namespace LibcMalloc
}  // namespace chap
