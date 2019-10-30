// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternRecognizer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class LongStringRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  typedef typename Allocations::TagHolder<Offset>::TagIndex TagIndex;
  LongStringRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "LongString"),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _tagIndex(~((TagIndex)(0))) {
    const LongStringAllocationsTagger<Offset>* tagger =
        processImage.GetLongStringAllocationsTagger();
    if (tagger != 0) {
      _tagIndex = tagger->GetCharsTagIndex();
    }
  }

  bool Matches(AllocationIndex index, const Allocation&, bool) const {
    return _tagHolder->GetTagIndex(index) == _tagIndex;
  }

  /*
   * If the address is matches any of the registered patterns, provide a
   * description for the address as belonging to that pattern
   * optionally with an additional explanation of why the address matches
   * the description.  Return true only if the allocation matches the
   * pattern.
   */
  virtual bool Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool,
                        bool explain) const {
    if (_tagHolder->GetTagIndex(index) != _tagIndex) {
      return false;
    }
    Commands::Output& output = context.GetOutput();
    const char* allocationImage;
    Offset numBytesFound = Base::_addressMap.FindMappedMemoryImage(
        allocation.Address(), &allocationImage);
    if (numBytesFound >= allocation.Size()) {
      Offset stringLength = (Offset)(strlen(allocationImage));
      output << "This allocation matches pattern LongString.\n";
      output << "The string has 0x" << std::hex << stringLength << " bytes, ";
      if (explain || stringLength < 77) {
        output << "containing\n\"" << allocationImage << "\".\n";
      } else {
        output << "starting with\n\"" << std::string(allocationImage, 77)
               << "\".\n";
      }
    }
    if (explain) {
      /* TODO: Identify the owner of the string. */
    }
    return true;
  }

 private:
  const Allocations::TagHolder<Offset>* _tagHolder;
  TagIndex _tagIndex;
};
}  // namespace chap
