// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternRecognizer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class ListNodeRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  typedef typename Allocations::TagHolder<Offset>::TagIndex TagIndex;
  ListNodeRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "ListNode"),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _tagIndex(~((TagIndex)(0))),
        _unknownHeadTagIndex(~((TagIndex)(0))) {
    const ListAllocationsTagger<Offset>* tagger =
        processImage.GetListAllocationsTagger();
    if (tagger != 0) {
      _tagIndex = tagger->GetNodeTagIndex();
      _unknownHeadTagIndex = tagger->GetUnknownHeadNodeTagIndex();
    }
  }

  bool Matches(AllocationIndex index, const Allocation&, bool) const {
    return MatchByIndexOnly(index);
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
    if (!MatchByIndexOnly(index)) {
      return false;
    }
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern ListNode.\n";
    TagIndex tagIndex = _tagHolder->GetTagIndex(index);
    if (tagIndex == _unknownHeadTagIndex) {
      output << "Warning: the header is not known for the list.\n";
    }
    if (explain) {
      if (tagIndex != _unknownHeadTagIndex) {
        /*
         * TODO: Identify the owner of list.  This can be done by scanning
         * backwards until we reach an address that is not the start of a list
         * node.
         */
      }
    }
    return true;
  }

 private:
  const Allocations::TagHolder<Offset>* _tagHolder;
  TagIndex _tagIndex;
  TagIndex _unknownHeadTagIndex;

  bool MatchByIndexOnly(AllocationIndex index) const {
    bool matches = false;
    if (_tagHolder != nullptr) {
      TagIndex tagIndex = _tagHolder->GetTagIndex(index);
      matches = (tagIndex == _tagIndex || tagIndex == _unknownHeadTagIndex);
    }
    return matches;
  }
};
}  // namespace chap
