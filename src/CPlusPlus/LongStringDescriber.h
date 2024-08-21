// Copyright (c) 2019,2020,2022,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string_view>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class LongStringDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  LongStringDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "LongString") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex /* index */,
                        const Allocation& allocation, bool explain) const {
    Commands::Output& output = context.GetOutput();
    const char* allocationImage;
    Offset numBytesFound = Base::_addressMap.FindMappedMemoryImage(
        allocation.Address(), &allocationImage);
    if (numBytesFound >= allocation.Size()) {
      std::string_view longString(allocationImage);
      output << "This allocation matches pattern LongString.\n";
      output << "The string has 0x" << std::hex << longString.size()
             << " bytes, ";
      if (explain || longString.size() < 77) {
        output << "containing\n\"" << longString << "\".\n";
      } else {
        output << "starting with\n\"" << longString.substr(0, 77)
               << "\".\n";
      }
    }
    if (explain) {
      /* TODO: Identify the owner of the string. */
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
