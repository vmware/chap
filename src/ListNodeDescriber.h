// Copyright (c) 2019,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class ListNodeDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  ListNodeDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "ListNode") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern ListNode.\n";
    if (explain) {
      size_t numEntries = 1;
      Offset address = allocation.Address();
      typename Allocations::TagHolder<Offset>::TagIndex tagIndex =
          Base::_tagHolder.GetTagIndex(index);
      typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
      AllocationIndex numAllocations = Base::_directory.NumAllocations();

      /*
       * Figure out the header by scanning back to what does not appear to
       * be the start of a %ListNode.  There are a few unexpected corner cases
       * where this could fail, such as when an allocation has an
       * std::list<T> as the first field, a T as the second field, and nothing
       * else, and so the header looks like the nodes in the list but often
       * even such a case can be worked around during pre-tagging by inspection
       * of incoming references.
       */
      Offset prev = reader.ReadOffset(address + sizeof(Offset), 0xbad);
      AllocationIndex prevIndex =
          Base::_graph->TargetAllocationIndex(index, prev);
      while (prevIndex != numAllocations &&
             Base::_tagHolder.GetTagIndex(prevIndex) == tagIndex &&
             Base::_directory.AllocationAt(prevIndex)->Address() == prev) {
        if (prev == address) {
          output << "This allocation belongs to an std::list but the header "
                    "can't be determined.\n";
          return;
        }
        numEntries++;
        address = prev;
        index = prevIndex;
        prev = reader.ReadOffset(address + sizeof(Offset), 0xbad);
        prevIndex = Base::_graph->TargetAllocationIndex(index, prev);
      }
      Offset header = prev;

      /*
       * Adjust the count of the list size by any nodes following the
       * given allocation.
       */
      for (Offset next = reader.ReadOffset(allocation.Address(), 0xbad);
           next != header; next = reader.ReadOffset(next, 0xbad)) {
        numEntries++;
      }
      output << "This allocation belongs to an std::list at 0x" << std::hex
             << header << "\nthat has " << std::dec << numEntries
             << ((numEntries == 1) ? " entry.\n" : " entries.\n");
    }
  }
};
}  // namespace chap
