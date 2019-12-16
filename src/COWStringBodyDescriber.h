// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/ContiguousImage.h"
#include "Allocations/PatternDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class COWStringBodyDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  COWStringBodyDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "COWStringBody"),
        _contiguousImage(*(processImage.GetAllocationFinder())) {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& /* allocation */,
                        bool explain) const {
    _contiguousImage.SetIndex(index);
    const Offset* asOffsets = _contiguousImage.FirstOffset();

    int numRefsMinus1 = *((int32_t*)(asOffsets + 2));

    Offset stringLength = asOffsets[0];
    const char* stringStart = (const char*)(asOffsets + 3);

    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern COWStringBody.\n";
    output << "This has capacity " << std::dec << asOffsets[1]
           << ", reference count " << (numRefsMinus1 + 1)
           << " and a string of size " << stringLength;
    if (explain || stringLength < 77) {
      output << " containing\n\"" << stringStart << "\".\n";
    } else {
      output << " starting with\n\"" << std::string(stringStart, 77) << "\",\n";
    }
    if (explain) {
      /*
       * TODO: separate any pointers to the c-string part (which are
       * valid references, from pointers to elsewhere in the string,
       * which are not.  Check both incoming references from free
       * allocations and anchors in that way and the sum of the counts
       * should match the reference count associated with the string body.
       * If the sum is smaller, this means that either one of the references
       * was corrupted or the containing object was sliced on destruction.
       */
    }
  }

 private:
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
};
}  // namespace chap
