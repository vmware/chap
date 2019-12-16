// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class ListNodeDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  ListNodeDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "ListNode") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex /* index */,
                        const Allocation&, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern ListNode.\n";
    if (explain) {
      /*
       * TODO: Identify the owner of list.  This can be done by scanning
       * backwards until we reach an address that is not the start of a list
       * node, except in cases where the head can't be found.
       */
    }
  }
};
}  // namespace chap
