// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <map>
#include "RangeMapper.h"

namespace chap {
template <typename Offset>
class ModuleDirectory {
 public:
  void AddModule(Offset baseAddress, Offset imageSize, std::string name) {
    _rangeMapper.MapRange(baseAddress, imageSize, name);
    if (!_rangesByName
             .insert(
                 std::make_pair(name, std::make_pair(baseAddress, imageSize)))
             .second) {
      if (name.empty()) {
        std::cerr << "Warning, there are at least two modules for which "
                     "the name is not known.\n";
      } else {
        std::cerr << "Warning, there are at least two modules for which "
                     "the name is \""
                  << name << "\".\n";
      }
    }
  }
  bool Find(const std::string& name, Offset& base, Offset& size) const {
    typename std::map<std::string, std::pair<Offset, Offset> >::const_iterator
        it = _rangesByName.find(name);
    if (it != _rangesByName.end()) {
      base = it->second.first;
      size = it->second.second;
      return true;
    } else {
      return false;
    }
  }

  bool Find(Offset addr, std::string& name, Offset& base, Offset& size) const {
    return _rangeMapper.FindRange(addr, base, size, name);
  }

 private:
  // The following allows determining which module, if any, is associated
  // with a given address.
  RangeMapper<Offset, std::string> _rangeMapper;
  // The following allows looking up memory ranges by short module name.
  std::map<std::string, std::pair<Offset, Offset> > _rangesByName;
};
}  // namespace chap
