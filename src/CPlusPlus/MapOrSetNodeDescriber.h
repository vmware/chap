// Copyright (c) 2019-2020,2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class MapOrSetNodeDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  MapOrSetNodeDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "MapOrSetNode") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern MapOrSetNode.\n";
    if (explain) {
      Offset address = allocation.Address();
      typename Allocations::TagHolder<Offset>::TagIndex tagIndex =
          Base::_tagHolder.GetTagIndex(index);
      typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
      AllocationIndex numAllocations = Base::_directory.NumAllocations();

      Offset parent = reader.ReadOffset(address + sizeof(Offset), 0xbad);
      AllocationIndex parentIndex =
          Base::_graph->TargetAllocationIndex(index, parent);
      while (parentIndex != numAllocations &&
             Base::_tagHolder.GetTagIndex(parentIndex) == tagIndex &&
             Base::_directory.AllocationAt(parentIndex)->Address() == parent) {
        address = parent;
        index = parentIndex;
        parent = reader.ReadOffset(address + sizeof(Offset), 0xbad);
        parentIndex = Base::_graph->TargetAllocationIndex(index, parent);
      }
      output << "This allocation belongs to an std::map or std::set at 0x"
             << std::hex << (parent - sizeof(Offset)) << "\nthat has "
             << std::dec << reader.ReadOffset(parent + 4 * sizeof(Offset), 0)
             << " entries.\n";
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
