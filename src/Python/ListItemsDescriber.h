// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace Python {
template <typename Offset>
class ListItemsDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  ListItemsDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage,
                                              "PythonListItems"),
        _infrastructureFinder(processImage.GetPythonInfrastructureFinder()),
        _contiguousImage(processImage.GetVirtualAddressMap(),
                         processImage.GetAllocationDirectory()) {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& /* allocation */,
                        bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern PythonListItems.\n";
    _contiguousImage.SetIndex(index);
    if (explain) {
    }
  }

 private:
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
};
}  // namespace Python
}  // namespace chap
