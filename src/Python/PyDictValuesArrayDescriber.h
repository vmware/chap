// Copyright (c) 2021 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/Graph.h"
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace Python {
template <typename Offset>
class PyDictValuesArrayDescriber
    : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  PyDictValuesArrayDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage,
                                              "PyDictValuesArray") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex /* index */,
                        const Allocation& /* allocation */,
                        bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern PyDictValuesArray.\n";
    output << "It contains values for a split python dict.\n";

    // TODO: Mention the number of entries in the array that are alive,
    // because the array may have been allocated larger than necessary.
    // This depends on looking at the dict keys object, which can be
    // reached by following an incoming edge back to a dictionary
    // then following an outgoing edge from that dictionary to
    // the keys object.

    if (explain) {
    }
  }

 private:
};
}  // namespace Python
}  // namespace chap
