// Copyright (c) 2019-2020,2022 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class UnorderedMapOrSetBucketsDescriber
    : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  UnorderedMapOrSetBucketsDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage,
                                              "UnorderedMapOrSetBuckets") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex /* index */,
                        const Allocation&, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern UnorderedMapOrSetBuckets.\n";
    if (explain) {
      /*
       * TODO: Identify the owner of the unordered map or unordered set.
       * The fastest way is to look for an anchor for the buckets or a
       * favored incoming edge.
       */
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
