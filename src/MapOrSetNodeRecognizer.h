// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternRecognizer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class MapOrSetNodeRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  typedef typename Allocations::TagHolder<Offset>::TagIndex TagIndex;
  MapOrSetNodeRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "MapOrSetNode"),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _tagIndex(~((TagIndex)(0))) {
    const MapOrSetAllocationsTagger<Offset>* tagger =
        processImage.GetMapOrSetAllocationsTagger();
    if (tagger != 0) {
      _tagIndex = tagger->GetNodeTagIndex();
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
    output << "This allocation matches pattern MapOrSetNode.\n";
    if (explain) {
      /*
       * TODO: Identify the owner of the map or set.  This can be done by
       * traveling up to a node for which the parent of the parent is itself.
       */
    }
    return true;
  }

 private:
  const Allocations::TagHolder<Offset>* _tagHolder;
  TagIndex _tagIndex;
};
}  // namespace chap