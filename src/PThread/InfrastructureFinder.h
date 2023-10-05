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
    // TODO: Consider using 0x700/0x710 or 0xb40/0xb48 to strengthen the check.
    // TODO: Consider useing 0x708 or 0xb44 for more precise information about
    // redirected pthreads.
    // TODO: Support pthread with externally supplied stack.
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
      const typename ModuleDirectory<Offset>::ModuleInfo& moduleInfo) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (const auto& range : moduleInfo._ranges) {
      int flags = range._value._flags;
      if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      Offset base = range._base;
      Offset limit = range._limit;

      for (Offset moduleAddr = base; moduleAddr < limit;
           moduleAddr += sizeof(Offset)) {
        Offset list0Header = moduleAddr;
        Offset list1Header = moduleAddr + 2 * sizeof(Offset);
        Offset list0First = moduleReader.ReadOffset(list0Header, 0xbad);
        if (list0First == 0 ||
            ((list0First & (sizeof(Offset) - 1)) != 0)) {
          continue;
        }
        Offset list0Last =
            moduleReader.ReadOffset(list0Header + sizeof(Offset), 0xbad);
        if (list0Last == 0 || ((list0Last & (sizeof(Offset) - 1)) != 0)) {
          continue;
        }
        Offset list1First =
            moduleReader.ReadOffset(list1Header, 0xbad);
        if (list1First == 0 ||
            ((list1First & (sizeof(Offset) - 1)) != 0)) {
          continue;
        }
        Offset list1Last =
            moduleReader.ReadOffset(list1Header + sizeof(Offset), 0xbad);
        if (list1Last == 0 ||
            ((list1Last & (sizeof(Offset) - 1)) != 0)) {
          continue;
        }
        if (list0First != list0Header) {
          if (list0Last == list0Header) {
            continue;
          }
          if ((list0First & 0xff) != (list0Last & 0xff)) {
            continue;
          }
          if (reader.ReadOffset(list0Last, 0xbad) != list0Header) {
            continue;
          }
          if (reader.ReadOffset(list0First + sizeof(Offset), 0xbad) !=
              list0Header) {
            continue;
          }
        } else {
          if (list0Last != list0Header) {
            continue;
          }
        }
        if (list1First != list1Header) {
          if (list1Last == list1Header) {
            continue;
          }
          if ((list1First & 0xff) != (list1Last & 0xff)) {
            continue;
          }
          if (list0First != list0Header &&
              (list0First & 0xff) != (list1First & 0xff)) {
            continue;
          }
          if (reader.ReadOffset(list1Last, 0xbad) != list1Header) {
            continue;
          }
          if (reader.ReadOffset(list1First + sizeof(Offset), 0xbad) !=
              list1Header) {
            continue;
          }
        } else {
          if (list1Last != list1Header) {
            continue;
          }
        }

        // TODO: Add more rigorous check if both lists are empty.
        _stacksFound = true;
        if (list0First != list0Header) {
          const char* stackType =
              (reader.Read32(list0First + 2 * sizeof(Offset), 0) > 0)
                  ? USED_PTHREAD_STACK
                  : CACHED_PTHREAD_STACK;
          if (!RegisterStacks(reader, list0Header, list0First,
                              stackType)) {
            if (!RegisterStacksBackwards(reader, list0Header, list0First,
                                         stackType)) {
            }
          }
        }
        if (list1First != list1Header) {
          const char* stackType =
              (reader.Read32(list1First + 2 * sizeof(Offset), 0) > 0)
                  ? USED_PTHREAD_STACK
                  : CACHED_PTHREAD_STACK;
          if (!RegisterStacks(reader, list1Header, list1First,
                              stackType)) {
            if (!RegisterStacksBackwards(reader, list1Header,
                                         list1First,
                                         stackType)) {
            }
          }
        }
      }
    }
  }
};
}  // namespace PThread
}  // namespace chap
