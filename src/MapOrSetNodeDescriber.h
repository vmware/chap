// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class MapOrSetNodeDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  MapOrSetNodeDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "MapOrSetNode") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex /* index */,
                        const Allocation&, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern MapOrSetNode.\n";
    if (explain) {
      /*
       * TODO: Identify the owner of the map or set.  This can be done by
       * traveling up to a node for which the parent of the parent is itself.
       */
    }
  }
};
}  // namespace chap
