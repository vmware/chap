// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <map>
#include "RangeMapper.h"
#include "VirtualMemoryPartition.h"

namespace chap {
template <typename Offset>
class ModuleDirectory {
 public:
  static const Offset MODULE_OFFSET_UNKNOWN = ~0;
  typedef RangeMapper<Offset, int> RangeToFlags;
  typedef typename std::map<std::string, RangeToFlags> NameToRanges;
  typedef typename NameToRanges::iterator iterator;
  typedef typename NameToRanges::const_iterator const_iterator;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  ModuleDirectory(VirtualMemoryPartition<Offset>& partition)
      : MODULE_ALIGNMENT_GAP("module alignment gap"),
        USED_BY_MODULE("used by module"),
        _virtualMemoryPartition(partition),
        _virtualAddressMap(partition.GetAddressMap()) {}
  void AddRange(Offset base, Offset size, std::string name) {
    if (!_rangeMapper.MapRange(base, size, name)) {
      std::cerr << "Warning, range [0x" << std::hex << base << ", 0x"
                << (base + size) << ")\nfor module " << name
                << "\noverlaps some other mapped range.\n";
      return;
    }
    RangeToFlags& rangesForModule = _rangesByName[name];
    Offset limit = base + size;
    auto itEnd = _virtualAddressMap.end();
    Offset subrangeBase = base;
    for (auto it = _virtualAddressMap.lower_bound(base);
         it != itEnd && it.Base() < limit; ++it) {
      if (subrangeBase < it.Base()) {
        subrangeBase = it.Base();
      }
      // [subrangeBase, it.Limit()) is known to the process image.
      Offset subrangeLimit = it.Limit();
      if (subrangeLimit > limit) {
        subrangeLimit = limit;
      }
      int flags = it.Flags();
      if ((flags &
           (RangeAttributes::IS_EXECUTABLE | RangeAttributes::IS_WRITABLE |
            RangeAttributes::IS_READABLE)) != 0) {
        // Count the range for now.  Actually registering the range in the
        // partition must be deferred at present both because there is
        // environment specific knowledge to calculate alignment gaps and
        // because such logic is best done after we know all the ranges
        // for the module.
        if (!rangesForModule.MapRange(subrangeBase,
                                      subrangeLimit - subrangeBase, flags)) {
          std::cerr << "Warning: failure to add [0x" << std::hex << subrangeBase
                    << ", 0x" << subrangeLimit << ") for module " << name
                    << "\n";
        }
      }
      subrangeBase = subrangeLimit;
    }
  }

  bool ExtendLastRange(Offset moduleBase, Offset newLimit) {
    Offset base;
    Offset size;
    std::string name;
    if (!_rangeMapper.FindRange(moduleBase, base, size, name)) {
      return false;
    }
    auto it = _rangesByName.find(name);
    if (it == _rangesByName.end()) {
      return false;
    }
    if (it->second.begin()->_base != base) {
      return false;
    }
    auto itLastRange = it->second.rbegin();
    Offset lastRangeLimit = itLastRange->_limit;
    if (lastRangeLimit < newLimit) {
      /*
       * The old limit was wrong and needs to be fixed.  First verify that the
       * old limit and new limit are mapped in the the same range or that the
       * new limit is at the end of the range containing the old one.
       */
      auto itMap = _virtualAddressMap.find(lastRangeLimit);
      if (itMap == _virtualAddressMap.end() || itMap.Limit() < newLimit) {
        return false;
      }
      if (!_rangeMapper.MapRange(lastRangeLimit, newLimit - lastRangeLimit,
                                 it->first)) {
        return false;
      }
      if (!it->second.MapRange(lastRangeLimit, (newLimit - lastRangeLimit),
                               itLastRange->_value)) {
        return false;
      }
    }

    return true;
  }

  RangeToFlags* Find(const std::string& name) const {
    const_iterator it = _rangesByName.find(name);
    if (it != _rangesByName.end()) {
      return it->second;
    } else {
      return (RangeToFlags*)(0);
    }
  }

  bool Find(Offset addr, std::string& name, Offset& base, Offset& size,
            Offset& relativeVirtualAddress) const {
    if (_rangeMapper.FindRange(addr, base, size, name)) {
      const_iterator it = _rangesByName.find(name);
      if (it != _rangesByName.end()) {
        relativeVirtualAddress = addr - (it->second.begin()->_base);
        int flags;
        if (it->second.FindRange(addr, base, size, flags)) {
          return true;
        }
      }
    }
    return false;
  }
  const_iterator begin() const { return _rangesByName.begin(); }
  const_iterator end() const { return _rangesByName.end(); }
  size_t NumModules() const { return _rangesByName.size(); }
  const char* MODULE_ALIGNMENT_GAP;
  const char* USED_BY_MODULE;

 private:
  // The following allows determining which module, if any, is associated
  // with a given address.
  RangeMapper<Offset, std::string> _rangeMapper;
  // The following allows looking up memory ranges by short module name.
  // Each entry in the map allows one to determine, for any range associated
  // with the corresponding module, the limits of that range and possibly,
  // if it is known, the start address relative to the binary.
  NameToRanges _rangesByName;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
};
}  // namespace chap
