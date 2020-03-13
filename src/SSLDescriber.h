// Copyright (c) 2018-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class SSLDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  SSLDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "SSL") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex /* index */,
                        const Allocation& /* allocation */,
                        bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern SSL.\n";
    if (explain) {
      output << "Offset " << sizeof(Offset)
             << " points to what appears to be an "
                " SSL_METHOD structure.\n";
    }
  }
};
}  // namespace chap
