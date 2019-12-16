// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class DequeBlockDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  DequeBlockDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "DequeBlock") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex /* index */,
                        const Allocation&, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern DequeBlock.\n";
    if (explain) {
      /*
       * TODO: Identify the owner of the deque, by following an incoming edge
       * back to the referencing map and from there to the deque itself.
       * Note that the map always is in an allocation but the deque itself
       * may also be statically allocated or on the stack.
       */
    }
  }
};
}  // namespace chap
