// Copyright (c) 2017-2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ProcessImage.h"
#include "Directory.h"
#include "Graph.h"
#include "PatternDescriber.h"

namespace chap {
namespace Allocations {
template <typename Offset>
class PatternDescriberRegistry {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  typedef typename TagHolder<Offset>::TagIndex TagIndex;
  typedef typename TagHolder<Offset>::TagIndices TagIndices;
  typedef typename std::multimap<std::string, PatternDescriber<Offset>*>
      DescriberMap;
  PatternDescriberRegistry(const ProcessImage<Offset>& processImage)
      : _tagHolder(*(processImage.GetAllocationTagHolder())),
        _numTags(_tagHolder.GetNumTags()) {
    _tagToDescribers.reserve(_numTags);
    _tagToDescribers.resize(_numTags);
  }

  void Register(PatternDescriber<Offset>& describer) {
    std::string fullTagName("%");
    fullTagName.append(describer.GetName());
    const typename TagHolder<Offset>::TagIndices* indices =
        _tagHolder.GetTagIndices(fullTagName);
    if (indices != nullptr) {
      for (TagIndex tagIndex : *indices) {
        _tagToDescribers[(size_t)(tagIndex)].push_back(&describer);
      }
    }
  }

  /*
  * If the allocation matches any of the registered patterns, provide a
  * description for the allocation as belonging to that pattern
  * optionally with an additional explanation of why the allocation matches
  * the description.
  */
  void Describe(Commands::Context& context, AllocationIndex index,
                const Allocation& allocation, bool /* isUnsigned */,
                bool explain) const {
    for (auto describer : _tagToDescribers[_tagHolder.GetTagIndex(index)]) {
      describer->Describe(context, index, allocation, explain);
    }
  }

  /*
   * Return a pointer to the TagIndices associated with the given pattern
   * name or none if no such pattern exists.
   */
  const TagIndices* GetTagIndices(const std::string& tagName) const {
    return (!tagName.empty() && tagName[0] == '%')
               ? _tagHolder.GetTagIndices(tagName)
               : nullptr;
  }

  const TagIndex GetTagIndex(AllocationIndex index) const {
    return _tagHolder.GetTagIndex(index);
  }

 private:
  const TagHolder<Offset>& _tagHolder;
  size_t _numTags;
  std::vector<std::list<PatternDescriber<Offset>*> > _tagToDescribers;
};
}  // namespace Allocations
}  // namespace chap
