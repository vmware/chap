// Copyright (c) 2019-2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <set>
#include <unordered_map>
#include "Directory.h"
#include "EdgePredicate.h"

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

  TagHolder(const AllocationIndex numAllocations,
            EdgePredicate<Offset>& edgeIsFavored,
            EdgePredicate<Offset>& edgeIsTainted)
      : _numAllocations(numAllocations),
        _edgeIsFavored(edgeIsFavored),
        _edgeIsTainted(edgeIsTainted) {
    _tags.reserve(numAllocations);
    _tags.resize(numAllocations, 0);
    _indexToName.push_back("");
    _tagIsStrong.push_back(false);
    _tagSupportsFavoredReferences.push_back(false);
  }

  TagIndex RegisterTag(const char* name, bool tagIsStrong,
                       bool tagSupportsFavoredReferences) {
    TagIndex newIndex = _indexToName.size();
    if (_indexToName.size() == 0x255) {
      std::cerr
          << "255 tags reached - change the implementation of TagHolder\n";
      abort();
    }
    _indexToName.push_back(name);
    _tagIsStrong.push_back(tagIsStrong);
    _tagSupportsFavoredReferences.push_back(tagSupportsFavoredReferences);
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
      if (oldTag != 0) {
        if (_tagSupportsFavoredReferences[oldTag]) {
          /*
           * The allocation was already tagged with a different tag (0 tag
           * does not support favored references) and the old tag supports
           * favored references.  Any references already favored based on
           * the old tag information are no longer considered favored.
           */
          _edgeIsFavored.SetAllIncoming(allocationIndex, false);
        }
        _edgeIsTainted.SetAllOutgoing(allocationIndex, false);
      }
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

  bool SupportsFavoredReferences(AllocationIndex allocationIndex) const {
    return (allocationIndex < _numAllocations &&
            _tagSupportsFavoredReferences[_tags[allocationIndex]]);
  }

  bool IsStronglyTagged(AllocationIndex allocationIndex) const {
    return (allocationIndex < _numAllocations &&
            _tagIsStrong[_tags[allocationIndex]]);
  }

 private:
  const AllocationIndex _numAllocations;
  EdgePredicate<Offset>& _edgeIsFavored;
  EdgePredicate<Offset>& _edgeIsTainted;
  std::vector<TagIndex> _tags;
  std::vector<std::string> _indexToName;
  std::vector<bool> _tagIsStrong;
  std::vector<bool> _tagSupportsFavoredReferences;
  std::unordered_map<std::string, TagIndices> _nameToTagIndices;
};
}  // namespace Allocations
}  // namespace chap
