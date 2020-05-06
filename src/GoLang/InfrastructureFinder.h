// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"

namespace chap {
namespace GoLang {
template <typename Offset>
class InfrastructureFinder {
 public:
  InfrastructureFinder(const ModuleDirectory<Offset>& moduleDirectory,
                       VirtualMemoryPartition<Offset>& partition)
      : _moduleDirectory(moduleDirectory),
        _isResolved(false),
        _virtualMemoryPartition(partition),
        _virtualAddressMap(partition.GetAddressMap()),
        _goRoutines(0),
        _numGoRoutines(0),
        _mheap(0) {}

  void Resolve() {
    if (_isResolved) {
      abort();
    }
    if (!_moduleDirectory.IsResolved()) {
      abort();
    }

    for (typename ModuleDirectory<Offset>::const_iterator it =
             _moduleDirectory.begin();
         it != _moduleDirectory.end(); ++it) {
      if (it->first.find(".so") != std::string::npos) {
        // For now, assume the go runtime code is not in a shared library.
        continue;
      }
      if (_goRoutines == 0) {
        FindGoRoutines(it->second);
      }
      // TODO: Find mheap_
    }
    if (_goRoutines != 0) {
      // TODO: Register goroutine stacks when the mechanism is a bit more
      // general.
      std::cerr << "Warning: This is a core for a GoLang process.\n"
                   "... GoLang allocations are not found yet.\n"
                   "... Stacks are not reported correctly yet.\n"
                   "... Native allocations using libc malloc are reported "
                   "correctly.\n";
    }
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  bool _isResolved;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  Offset _goRoutines;
  Offset _numGoRoutines;
  Offset _mheap;

  bool HasApparentGoRoutinePointer(Reader& reader, Offset pointerAddress) {
    Offset goRoutine = reader.ReadOffset(pointerAddress, 0xbad);
    if ((goRoutine & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    if (reader.ReadOffset(goRoutine + 9 * sizeof(Offset), 0xbad) != goRoutine) {
      return false;
    }
    Offset stackBase = reader.ReadOffset(goRoutine, 0xbad);
    if ((stackBase & 0x3f) != 0) {
      return false;
    }
    Offset stackLimit = reader.ReadOffset(goRoutine + sizeof(Offset), 0xbad);
    if ((stackBase & 0x3f) != 0) {
      return false;
    }
    if (stackBase == 0) {
      if (stackLimit != 0) {
        return false;
      }
    } else {
      if (stackLimit <= stackBase) {
        return false;
      }
      Offset stackPointer =
          reader.ReadOffset(goRoutine + 7 * sizeof(Offset), 0);
      if (stackPointer < stackBase || stackPointer >= stackLimit) {
        return false;
      }
    }
    return true;
  }

  void FindGoRoutines(
      const typename ModuleDirectory<Offset>::RangeToFlags& rangeToFlags) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (typename ModuleDirectory<Offset>::RangeToFlags::const_iterator
             itRange = rangeToFlags.begin();
         itRange != rangeToFlags.end(); ++itRange) {
      int flags = itRange->_value;
      if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      Offset base = itRange->_base;
      /*
       * At present the module finding logic can get a lower value for the
       * limit than the true limit.  It is conservative about selecting the
       * limit to avoid tagging too large a range in the partition.  However
       * this conservative estimate is problematic if the pointer to the
       * arena struct array lies between the calculated limit and the real
       * limit.  This code works around this to extend the limit to the
       * last consecutive byte that has the same permission as the last
       * byte in the range.
       */
      Offset limit = _virtualAddressMap.find(itRange->_limit - 1).Limit();

      for (Offset moduleAddr = base; moduleAddr < limit;
           moduleAddr += sizeof(Offset)) {
        Offset arrayOfPointers = moduleReader.ReadOffset(moduleAddr, 0xbad);
        if ((arrayOfPointers & 7) != 0) {
          continue;
        }
        Offset size = moduleReader.ReadOffset(moduleAddr + sizeof(Offset), 0);
        Offset capacity =
            moduleReader.ReadOffset(moduleAddr + 2 * sizeof(Offset), 0);
        if (size < 4 || size > capacity) {
          continue;
        }

        if (HasApparentGoRoutinePointer(reader, arrayOfPointers) &&
            HasApparentGoRoutinePointer(
                reader, arrayOfPointers + (size - 1) * sizeof(Offset))) {
          _goRoutines = arrayOfPointers;
          _numGoRoutines = size;
          return;
        }
      }
    }
  }
};
}  // namespace GoLang
}  // namespace chap
