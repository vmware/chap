// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <cstdlib>
#include "RangeMapper.h"
#include "VirtualMemoryPartition.h"

/*
 * This provides partial information about memory ranges that are mapped to
 * ranges in files.  In many cases these will be associated with the main
 * executable or with shared libraries, but the mappings could also have
 * been established directly by a call to mmap.  That distinction is not
 * made here, but the ModuleDirectory will not contain ranges that are
 * not associated with modules.  The set of ranges here may not be complete
 * because the process image may not reflect the existence of at least some
 * of those ranges.
 */

namespace chap {
template <typename Offset>
class FileMappedRangeDirectory {
 public:
  struct RangeInfo {
    RangeInfo(const std::string& path, Offset offsetInFile, int flags)
        : _path(path), _offsetInFile(offsetInFile), _flags(flags) {}
    RangeInfo() : _offsetInFile(0), _flags(0) {}
    RangeInfo& operator=(const RangeInfo& other) {
      if (this != &other) {
        _path = other._path;
        _offsetInFile = other._offsetInFile;
        _flags = other._flags;
      }
      return *this;
    }
    bool operator==(const RangeInfo& other) {
      return _path == other._path && _offsetInFile == other._offsetInFile &&
             _flags == other._flags;
    }

    std::string _path;
    Offset _offsetInFile;
    int _flags;  // These match those in VirtualAddressMap.
  };
  typedef
      typename RangeMapper<Offset, RangeInfo>::const_iterator const_iterator;
  typedef typename RangeMapper<Offset, RangeInfo>::const_reverse_iterator
      const_reverse_iterator;

  FileMappedRangeDirectory(VirtualMemoryPartition<Offset>& partition)
      : FILE_MAPPED_RANGE("file mapped range"),
        _ranges(false),
        _isResolved(false),
        _virtualMemoryPartition(partition),
        _virtualAddressMap(partition.GetAddressMap()) {}
  void AddRange(Offset base, Offset size, const std::string& path,
                Offset offsetInFile, int flags) {
    if (_isResolved) {
      // The module directory cannot be changed after it has been resolved.
      abort();
    }
    if (!_ranges.MapRange(base, size, RangeInfo(path, offsetInFile, flags))
             .second) {
      abort();
    }
  }

  const_iterator find(Offset member) const { return _ranges.find(member); }
  const_iterator begin() const { return _ranges.begin(); }
  const_iterator end() const { return _ranges.end(); }
  const_iterator rbegin() const { return _ranges.rbegin(); }
  const_iterator rend() const { return _ranges.rend(); }
  bool empty() const { return (_ranges.empty()); }

  /*
   * This returns an iterator to the first range with limit after the given
   * member, or an iterator to the end if no such range exists.
   */
  const_iterator upper_bound(Offset member) const {
    return _ranges.upper_bound(member);
  }

  const char* FILE_MAPPED_RANGE;

 private:
  RangeMapper<Offset, RangeInfo> _ranges;
  bool _isResolved;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
};
}  // namespace chap
