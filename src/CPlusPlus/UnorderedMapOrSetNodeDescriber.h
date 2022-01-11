// Copyright (c) 2019-2020,2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class UnorderedMapOrSetNodeDescriber
    : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  UnorderedMapOrSetNodeDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage,
                                              "UnorderedMapOrSetNode") {}
  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex /* index */,
                        const Allocation&, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern UnorderedMapOrSetNode.\n";
    if (explain) {
      /*
       * TODO: Identify the owner of the unordered map or unordered set.
       * The fastest way is to scan back for the buckets, possibly passing a
       * very small number of nodes then look for how the buckets array is
       * anchored.  Note that for a non-empty unordered map or unordered set
       * there will be one node that has no incoming edge from the buckets array
       * but that has and incoming edge from the unordered map or unordered set
       * header.
       */
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
