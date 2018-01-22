// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <map>
#include <set>

/*
 * This keeps mappings from anchor to name and name to set of anchors.
 * Note that there are potentially multiple anchors (numbers) for a given
 * name because a anchor may be defined in multiple load modules.
 */

namespace chap {
namespace Allocations {
template <class Offset>
class AnchorDirectory {
 public:
  typedef std::map<Offset, std::string> AnchorToNameMap;
  typedef typename AnchorToNameMap::iterator AnchorToNameIterator;
  typedef typename AnchorToNameMap::const_iterator AnchorToNameConstIterator;
  typedef std::map<std::string, std::set<Offset> > NameToAnchorsMap;
  typedef typename NameToAnchorsMap::const_iterator NameToAnchorsConstIterator;

  AnchorDirectory() : _multipleAnchorsPerName(false) {}

  void MapAnchorToName(Offset anchor, std::string name) {
    AnchorToNameIterator it = _anchorToName.find(anchor);
    if (it != _anchorToName.end()) {
      /*
       * This anchor is already known to be an anchor.
       */
      if (it->second == name || name.empty()) {
        /*
         * There is no new information about the name.
         */
        return;
      }
      if (!(it->second).empty()) {
        /*
         * There was a previously known name, which is now no longer
         * associated with the anchor.
         */
        _nameToAnchors[it->second].erase(anchor);
      }
      it->second = name;
    } else {
      _anchorToName[anchor] = name;
    }
    if (!name.empty()) {
      std::set<Offset>& anchors = _nameToAnchors[name];
      anchors.insert(anchor);
      if (anchors.size() > 1) {
        _multipleAnchorsPerName = true;
      }
    }
  }

  bool HasMultipleAnchorsPerName() const { return _multipleAnchorsPerName; }

  bool IsMapped(Offset anchor) const {
    return _anchorToName.find(anchor) != _anchorToName.end();
  }

  const std::string& Name(Offset anchor) const {
    AnchorToNameConstIterator it = _anchorToName.find(anchor);
    if (it != _anchorToName.end()) {
      return it->second;
    } else {
      return NO_NAME;
    }
  }

  const std::set<Offset>& Anchors(const std::string& name) const {
    typename NameToAnchorsMap::const_iterator it = _nameToAnchors.find(name);
    if (it != _nameToAnchors.end()) {
      return it->second;
    } else {
      return NO_ANCHORS;
    }
  }

 private:
  bool _multipleAnchorsPerName;
  AnchorToNameMap _anchorToName;
  NameToAnchorsMap _nameToAnchors;
  std::string NO_NAME;
  std::set<Offset> NO_ANCHORS;
};
}  // namespace Allocations
}  // namespace chap
