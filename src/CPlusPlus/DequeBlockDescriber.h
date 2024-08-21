// Copyright (c) 2019,2020,2022 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class DequeBlockDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
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
       * Use a favored reference to reach the map and an again to reach the
       * deque, if it is part of an allocation.
       * TODO: maybe the notion of a favored anchor would be of value.
       */
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
