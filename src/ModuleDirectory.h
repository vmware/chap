// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <map>
#include "RangeMapper.h"

namespace chap {
template <typename Offset>
class ModuleDirectory {
 public:
  static const Offset MODULE_OFFSET_UNKNOWN = ~0;
  typedef typename std::map<std::string, RangeMapper<Offset, Offset> >
      NameToRanges;
  typedef typename NameToRanges::iterator iterator;
  typedef typename NameToRanges::const_iterator const_iterator;
  void AddRange(Offset baseAddress, Offset imageSize, std::string name,
                Offset offsetInModule) {
    if (!_rangeMapper.MapRange(baseAddress, imageSize, name)) {
      std::cerr << "Warning, range [0x" << std::hex << baseAddress << ", 0x"
                << (baseAddress + imageSize) << ")\nfor module " << name
                << "\noverlaps some other mapped range.\n";
      return;
    }
    RangeMapper<Offset, Offset>& rangesForModule = _rangesByName[name];
    if (!rangesForModule.MapRange(baseAddress, imageSize, offsetInModule)) {
      /*
       * It should be impossible to reach this, due to the fact that the
       * given range already was known not to overlap ranges for any module
       * not just ranges for this one.
       */
      abort();
    }
  }

  void AddRange(Offset baseAddress, Offset imageSize, std::string name) {
    AddRange(baseAddress, imageSize, name, MODULE_OFFSET_UNKNOWN);
  }

  RangeMapper<Offset, Offset>* Find(const std::string& name) const {
    const_iterator it = _rangesByName.find(name);
    if (it != _rangesByName.end()) {
      return it->second;
    } else {
      return (RangeMapper<Offset, Offset>*)(0);
    }
  }

  bool Find(Offset addr, std::string& name, Offset& base, Offset& size,
            Offset& fileOffset, Offset& relativeVirtualAddress) const {
    if (_rangeMapper.FindRange(addr, base, size, name)) {
      const_iterator it = _rangesByName.find(name);
      if (it != _rangesByName.end()) {
        relativeVirtualAddress = addr - (it->second.begin()->_base);
        Offset regionOffsetInModule;
        if (it->second.FindRange(addr, base, size, regionOffsetInModule)) {
          if (regionOffsetInModule == MODULE_OFFSET_UNKNOWN) {
            fileOffset = MODULE_OFFSET_UNKNOWN;
          } else {
            fileOffset = (addr - base) + regionOffsetInModule;
          }
          return true;
        }
      }
    }
    return false;
  }
  const_iterator begin() const { return _rangesByName.begin(); }
  const_iterator end() const { return _rangesByName.end(); }
  size_t NumModules() const { return _rangesByName.size(); }

 private:
  // The following allows determining which module, if any, is associated
  // with a given address.
  RangeMapper<Offset, std::string> _rangeMapper;
  // The following allows looking up memory ranges by short module name.
  // Each entry in the map allows one to determine, for any range associated
  // with the corresponding module, the limits of that range and possibly,
  // if it is known, the start address relative to the binary.
  std::map<std::string, RangeMapper<Offset, Offset> > _rangesByName;
};
}  // namespace chap
