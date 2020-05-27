// Copyright (c) 2019-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <set>
#include <unordered_map>
#include "Directory.h"

namespace chap {
namespace Allocations {
template <typename Offset>
class TagHolder {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef size_t TagIndex;

  /*
   * Note that the sets of tag indices tend to be tiny, usually with just
   * one element, because they are all the indices corresponding to a
   * single name.
   */
  typedef std::set<TagIndex> TagIndices;

  TagHolder(const AllocationIndex numAllocations)
      : _numAllocations(numAllocations) {
    _tags.reserve(numAllocations);
    _tags.resize(numAllocations, 0);
    _indexToName.push_back("");
    _tagIsStrong.push_back(false);
  }

  TagIndex RegisterTag(const char* name, bool tagIsStrong = true) {
    TagIndex newIndex = _indexToName.size();
    if (_indexToName.size() == 0x255) {
      std::cerr
          << "255 tags reached - change the implementation of TagHolder\n";
      abort();
    }
    _indexToName.push_back(name);
    _tagIsStrong.push_back(tagIsStrong);
    _nameToTagIndices[name].insert(newIndex);
    return newIndex;
  }

  bool TagAllocation(AllocationIndex allocationIndex, TagIndex tagIndex) {
    if (tagIndex >= _indexToName.size()) {
      std::cerr << "Invalid allocation tag index " << tagIndex << "\n";
      abort();
    }
    if (allocationIndex >= _numAllocations) {
      std::cerr << "Invalid allocation index " << allocationIndex << "\n";
      abort();
    }
    TagIndex oldTag = _tags[allocationIndex];
    if (oldTag == 0 || (_tagIsStrong[tagIndex] && !_tagIsStrong[oldTag])) {
      _tags[allocationIndex] = tagIndex;
      return true;
    }
    return false;
  }

  TagIndex GetTagIndex(AllocationIndex allocationIndex) const {
    if (allocationIndex >= _numAllocations) {
      std::cerr << "Invalid allocation index " << allocationIndex << "\n";
      abort();
    }
    return _tags[allocationIndex];
  }

  const std::string& GetTagName(AllocationIndex allocationIndex) const {
    if (allocationIndex >= _numAllocations) {
      std::cerr << "Invalid allocation index " << allocationIndex << "\n";
      abort();
    }
    return _indexToName[_tags[allocationIndex]];
  }

  const TagIndices* GetTagIndices(std::string tagName) const {
    std::unordered_map<std::string, TagIndices>::const_iterator it =
        _nameToTagIndices.find(tagName);
    return (it == _nameToTagIndices.end()) ? nullptr : &(it->second);
  }

  size_t GetNumTags() const { return _indexToName.size(); }

  bool IsStronglyTagged(AllocationIndex allocationIndex) const {
    return (allocationIndex < _numAllocations &&
            _tagIsStrong[_tags[allocationIndex]]);
  }

 private:
  const AllocationIndex _numAllocations;
  std::vector<TagIndex> _tags;
  std::vector<std::string> _indexToName;
  std::vector<bool> _tagIsStrong;
  std::unordered_map<std::string, TagIndices> _nameToTagIndices;
};
}  // namespace Allocations
}  // namespace chap
