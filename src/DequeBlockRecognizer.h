// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternRecognizer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class DequeBlockRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  typedef typename Allocations::TagHolder<Offset>::TagIndex TagIndex;
  DequeBlockRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "DequeBlock"),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _tagIndex(~((TagIndex)(0))) {
    const DequeAllocationsTagger<Offset>* tagger =
        processImage.GetDequeAllocationsTagger();
    if (tagger != 0) {
      _tagIndex = tagger->GetBlockTagIndex();
    }
  }

  bool Matches(AllocationIndex index, const Allocation&, bool) const {
    return _tagHolder != nullptr && _tagHolder->GetTagIndex(index) == _tagIndex;
  }

  /*
   * If the address is matches any of the registered patterns, provide a
   * description for the address as belonging to that pattern
   * optionally with an additional explanation of why the address matches
   * the description.  Return true only if the allocation matches the
   * pattern.
   */
  virtual bool Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation&, bool, bool explain) const {
    if (_tagHolder == nullptr || _tagHolder->GetTagIndex(index) != _tagIndex) {
      return false;
    }
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern DequeBlock.\n";
    if (explain) {
      /*
       * TODO: Identify the owner of the deque, by following an incoming edge
       * back to the referencing map and from there to the deque itself.
       * Note that the map always is in an allocation but the deque itself
       * may also be statically allocated or on the stack.
       */
    }
    return true;
  }

 private:
  const Allocations::TagHolder<Offset>* _tagHolder;
  TagIndex _tagIndex;
};
}  // namespace chap
