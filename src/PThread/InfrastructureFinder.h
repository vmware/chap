// Copyright (c) 2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ModuleDirectory.h"
#include "../StackRegistry.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"

namespace chap {
namespace PThread {
template <typename Offset>
class InfrastructureFinder {
 public:
  InfrastructureFinder(const ModuleDirectory<Offset>& moduleDirectory,
                       VirtualMemoryPartition<Offset>& partition,
                       StackRegistry<Offset>& stackRegistry)
      : USED_PTHREAD_STACK("used pthread stack"),
        CACHED_PTHREAD_STACK("cached pthread stack"),
        PTHREAD_STACK_OVERFLOW_GUARD("pthread stack overflow guard"),
        _moduleDirectory(moduleDirectory),
        _isResolved(false),
        _pthreadLibraryPresent(false),
        _stacksFound(false),
        _virtualMemoryPartition(partition),
        _stackRegistry(stackRegistry),
        _virtualAddressMap(partition.GetAddressMap()) {}

  const char* USED_PTHREAD_STACK;
  const char* CACHED_PTHREAD_STACK;
  const char* PTHREAD_STACK_OVERFLOW_GUARD;

  void Resolve() {
    // TODO: Immediately after the double links for each PThread, the LWP is
    // found.
    // TODO: Consider using 0x700, 0x710 to strengthen the check.
    // TODO: Consider useing 0x708 for more precise information about redirected
    // pthreads.
    if (_isResolved) {
      abort();
    }
    if (!_moduleDirectory.IsResolved()) {
      abort();
    }

    typename ModuleDirectory<Offset>::const_iterator itEnd =
        _moduleDirectory.end();
    typename ModuleDirectory<Offset>::const_iterator it =
        _moduleDirectory.begin();
    for (; it != _moduleDirectory.end(); ++it) {
      if ((it->first.find("pthread") == std::string::npos)) {
        continue;
      }
      _pthreadLibraryPresent = true;
      FindStacks(it->second);
      if (_stacksFound) {
        break;
      }
    }
    if (!_stacksFound) {
      for (; it != _moduleDirectory.end(); ++it) {
        if (it->first.find(".so") != std::string::npos) {
          // For now, assume the go runtime code is not in a shared library.
          continue;
        }
        FindStacks(it->second);
        if (_stacksFound) {
          break;
        }
      }
    }
    if (_pthreadLibraryPresent && !_stacksFound) {
      std::cerr << "Warning: a pthread library appears to be in use but the "
                   "pthread stack lists were not found.\n";
    }
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  bool _isResolved;
  bool _pthreadLibraryPresent;
  bool _stacksFound;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  StackRegistry<Offset>& _stackRegistry;
  const VirtualAddressMap<Offset>& _virtualAddressMap;

  bool RegisterStackAndClaimStackRange(Offset linkInChain,
                                       const char* stackType) {
    typename VirtualAddressMap<Offset>::const_iterator it =
        _virtualAddressMap.find(linkInChain);
    if (it == _virtualAddressMap.end()) {
      std::cerr << "Process image does not contain mapping for " << stackType
                << " that contains address 0x" << std::hex << linkInChain
                << "\n";
      return false;
    }
    if (it.GetImage() == (const char*)(0)) {
      std::cerr << "Process image does not contain image for " << stackType
                << " that contains address 0x" << std::hex << linkInChain
                << "\n";
      return false;
    }
    Offset base = it.Base();
    Offset limit = (linkInChain + 0xfff) & ~0xfff;
    if (!_virtualMemoryPartition.ClaimRange(base, limit - base, stackType,
                                            false)) {
      std::cerr << "Warning: Failed to claim " << stackType << " [" << std::hex
                << base << ", " << limit << ") due to overlap.\n";
    }
    if (!_stackRegistry.RegisterStack(base, limit, stackType)) {
      std::cerr << "Warning: Failed to register " << stackType << " ["
                << std::hex << base << ", " << limit
                << ") due to overlap with other stack.\n";
    }
    if (!_virtualMemoryPartition.ClaimRange(
            base - 0x1000, 0x1000, PTHREAD_STACK_OVERFLOW_GUARD, false)) {
      std::cerr << "Warning: Failed to claim " << PTHREAD_STACK_OVERFLOW_GUARD
                << " [" << std::hex << (base - 0x1000) << ", " << base
                << ") due to overlap.\n";
    }
    return true;
  }

  bool RegisterStacks(Reader& reader, Offset listHeader, Offset firstInChain,
                      const char* stackType) {
    for (Offset linkInChain = firstInChain; linkInChain != listHeader;
         linkInChain = reader.ReadOffset(linkInChain, 0xbad)) {
      if ((linkInChain & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      if (!RegisterStackAndClaimStackRange(linkInChain, stackType)) {
        return false;
      }
    }
    return true;
  }
  bool RegisterStacksBackwards(Reader& reader, Offset listHeader,
                               Offset lastInChain, const char* stackType) {
    for (Offset linkInChain = lastInChain; linkInChain != listHeader;
         linkInChain = reader.ReadOffset(linkInChain + sizeof(Offset), 0xbad)) {
      if ((linkInChain & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      if (!RegisterStackAndClaimStackRange(linkInChain, stackType)) {
        return false;
      }
    }
    return true;
  }
  void FindStacks(
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
       * this conservative estimate is problematic if the array header we
       * are seeking lies between the calculated limit and the real
       * limit.  This code works around this to extend the limit to the
       * last consecutive byte that has the same permission as the last
       * byte in the range.
       */
      Offset limit = _virtualAddressMap.find(itRange->_limit - 1).Limit() -
                     3 * sizeof(Offset);

      for (Offset moduleAddr = base; moduleAddr < limit;
           moduleAddr += sizeof(Offset)) {
        Offset usedListHeader = moduleAddr;
        Offset cachedListHeader = moduleAddr + 2 * sizeof(Offset);
        Offset usedListFirst = moduleReader.ReadOffset(usedListHeader, 0xbad);
        if (usedListFirst == 0 ||
            ((usedListFirst & (sizeof(Offset) - 1)) != 0)) {
          continue;
        }
        Offset usedListLast =
            moduleReader.ReadOffset(usedListHeader + sizeof(Offset), 0xbad);
        if (usedListLast == 0 || ((usedListLast & (sizeof(Offset) - 1)) != 0)) {
          continue;
        }
        Offset cachedListFirst =
            moduleReader.ReadOffset(cachedListHeader, 0xbad);
        if (cachedListFirst == 0 ||
            ((cachedListFirst & (sizeof(Offset) - 1)) != 0)) {
          continue;
        }
        Offset cachedListLast =
            moduleReader.ReadOffset(cachedListHeader + sizeof(Offset), 0xbad);
        if (cachedListLast == 0 ||
            ((cachedListLast & (sizeof(Offset) - 1)) != 0)) {
          continue;
        }
        if (usedListFirst != usedListHeader) {
          if (usedListLast == usedListHeader) {
            continue;
          }
          if ((usedListFirst & 0xff) != (usedListLast & 0xff)) {
            continue;
          }
        } else {
          if (usedListLast != usedListHeader) {
            continue;
          }
        }
        if (cachedListFirst != cachedListHeader) {
          if (cachedListLast == cachedListHeader) {
            continue;
          }
          if ((cachedListFirst & 0xff) != (cachedListLast & 0xff)) {
            continue;
          }
          if (usedListFirst != usedListHeader &&
              (usedListFirst & 0xff) != (cachedListFirst & 0xff)) {
            continue;
          }
        } else {
          if (cachedListLast != cachedListHeader) {
            continue;
          }
        }
        // TODO: Add more rigorous check if both lists are empty.
        _stacksFound = true;
        if (usedListFirst != usedListHeader) {
          if (!RegisterStacks(reader, usedListHeader, usedListFirst,
                              USED_PTHREAD_STACK)) {
            if (!RegisterStacksBackwards(reader, usedListHeader, usedListFirst,
                                         USED_PTHREAD_STACK)) {
            }
          }
        }
        if (cachedListFirst != cachedListHeader) {
          if (!RegisterStacks(reader, cachedListHeader, cachedListFirst,
                              CACHED_PTHREAD_STACK)) {
            if (!RegisterStacksBackwards(reader, cachedListHeader,
                                         cachedListFirst,
                                         CACHED_PTHREAD_STACK)) {
            }
          }
        }
      }
    }
  }
};
}  // namespace PThread
}  // namespace chap
