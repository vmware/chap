// Copyright (c) 2021-2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <unordered_set>
#include "../ModuleDirectory.h"
#include "../StackRegistry.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"

namespace chap {
namespace FollyFibers {
template <typename Offset>
class InfrastructureFinder {
 public:
  InfrastructureFinder(const ModuleDirectory<Offset>& moduleDirectory,
                       VirtualMemoryPartition<Offset>& partition,
                       StackRegistry<Offset>& stackRegistry)
      : FOLLY_FIBER_STACK("folly fiber stack"),
        FOLLY_FIBER_STACK_OVERFLOW_GUARD("folly fiber stack overflow guard"),
        _moduleDirectory(moduleDirectory),
        _isResolved(false),
        _follyLibraryPresent(false),
        _stacksFound(false),
        _virtualMemoryPartition(partition),
        _stackRegistry(stackRegistry),
        _virtualAddressMap(partition.GetAddressMap()) {}

  const char* FOLLY_FIBER_STACK;
  const char* FOLLY_FIBER_STACK_OVERFLOW_GUARD;

  void Resolve() {
    if (_isResolved) {
      abort();
    }
    if (!_moduleDirectory.IsResolved()) {
      abort();
    }

    for (const auto& modulePathAndInfo : _moduleDirectory) {
      if (modulePathAndInfo.first.find("libfolly") == std::string::npos) {
        continue;
      }
      _follyLibraryPresent = true;
      if (FindAndRegisterStacks(modulePathAndInfo.second)) {
        _stacksFound = true;
        break;
      }
    }
    if (!_stacksFound) {
      for (const auto& modulePathAndInfo : _moduleDirectory) {
        if (modulePathAndInfo.first.find(".so") != std::string::npos) {
          /*
           * For now, assume the folly fibers runtime code is not in a shared
           * library.
           */
          continue;
        }
        if (FindAndRegisterStacks(modulePathAndInfo.second)) {
          _stacksFound = true;
        }
      }
    }
    if (_follyLibraryPresent && !_stacksFound) {
      std::cerr << "Warning: a folly library appears to be in use but the "
                   "associated stacks were not found.\n";
    }
    if (!_stacksFound) {
      _stacks.clear();
      _unresolvedStackIndices.clear();
    }
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  bool _isResolved;
  bool _follyLibraryPresent;
  bool _stacksFound;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  StackRegistry<Offset>& _stackRegistry;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  struct StackInfo {
    StackInfo(Offset guardBase, Offset stackBase, Offset stackLimit,
              bool isResolved)
        : _guardBase(guardBase),
          _stackBase(stackBase),
          _stackLimit(stackLimit),
          _isResolved(isResolved) {}
    Offset _guardBase;
    Offset _stackBase;
    Offset _stackLimit;
    bool _isResolved;
  };
  std::vector<StackInfo> _stacks;
  std::vector<size_t> _unresolvedStackIndices;
  std::unordered_set<Offset> _acceptedReturnAddresses;

  bool FindStacksFromGuardSetHolder(Reader& reader, Offset mayHaveGuardSet) {
    if (mayHaveGuardSet == 0 ||
        ((mayHaveGuardSet & (sizeof(Offset) - 1)) != 0)) {
      return false;
    }
    Offset mayBeFirstGuardNode =
        reader.ReadOffset(mayHaveGuardSet + 2 * sizeof(Offset), 0xbad);
    if ((mayBeFirstGuardNode == 0) ||
        ((mayBeFirstGuardNode & (sizeof(Offset) - 1)) != 0)) {
      return false;
    }
    Offset mayBeGuardNode = mayBeFirstGuardNode;
    do {
      Offset mayBeGuardBase =
          reader.ReadOffset(mayBeGuardNode + sizeof(Offset), 0xbad);
      if (mayBeGuardBase == 0 || ((mayBeGuardBase & 0xfff) != 0)) {
        return false;
      }
      Offset mayBeGuardLimit =
          reader.ReadOffset(mayBeGuardNode + 2 * sizeof(Offset), 0xbad);
      if (mayBeGuardLimit == 0 || ((mayBeGuardLimit & 0xfff) != 0)) {
        return false;
      }
      if (mayBeGuardLimit <= mayBeGuardBase) {
        return false;
      }
      mayBeGuardNode = reader.ReadOffset(mayBeGuardNode, 0xbad);
    } while ((mayBeGuardNode != 0) &&
             ((mayBeFirstGuardNode & (sizeof(Offset) - 1)) == 0));

    if (mayBeGuardNode != 0) {
      return false;
    }

    _stacks.clear();
    _unresolvedStackIndices.clear();
    mayBeGuardNode = mayBeFirstGuardNode;

    do {
      Offset mayBeGuardBase =
          reader.ReadOffset(mayBeGuardNode + sizeof(Offset), 0xbad);
      Offset mayBeGuardLimit =
          reader.ReadOffset(mayBeGuardNode + 2 * sizeof(Offset), 0xbad);
      typename VirtualAddressMap<Offset>::const_iterator it =
          _virtualAddressMap.find(mayBeGuardBase);
      if (it == _virtualAddressMap.end()) {
        if (_follyLibraryPresent) {
          std::cerr << "Process image does not contain mapping for folly "
                       "fiber stack guard"
                    << " that contains address 0x" << std::hex << mayBeGuardBase
                    << "\n";
        }
        return false;
      }
      /*
       * There might possibly be an inaccessible region before the guard
       * but none is expected after it.
       */
      if (it.Limit() != mayBeGuardLimit) {
        return false;
      }
      int guardFlags = it.Flags();
      /*
       * The guard region is really supposed to be inaccessible, but some
       *  variants of gdb have a bug that cause it to appear in the core
       * as read only.
       */
      if ((guardFlags & RangeAttributes::IS_WRITABLE) != 0 ||
          (guardFlags & RangeAttributes::IS_EXECUTABLE) != 0) {
        return false;
      }

      ++it;
      if (it == _virtualAddressMap.end()) {
        return false;
      }

      if (it.Base() != mayBeGuardLimit) {
        return false;
      }

      /*
       * The stack itself should be writable.
       */
      int stackFlags = it.Flags();
      if ((stackFlags & RangeAttributes::IS_WRITABLE) == 0) {
        return false;
      }

      /*
       * The stack may be adjacent to another writable region.  The last
       * quadword of the stack is expected to point to executable memory.
       */

      _stacks.emplace_back(mayBeGuardBase, mayBeGuardLimit, it.Limit(), false);
      mayBeGuardNode = reader.ReadOffset(mayBeGuardNode, 0xbad);
    } while (mayBeGuardNode != 0);
    return true;
  }

  bool FindStacks(
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
        if (FindStacksFromGuardSetHolder(
                reader, moduleReader.ReadOffset(moduleAddr, 0xbad))) {
          return true;
        }
      }
    }
    return false;
  }

  bool ResolveStackLimit(StackInfo& stackInfo) {
    Reader reader(_virtualAddressMap);
    size_t numNewReturnsFound = 0;
    Offset possibleNewReturn = 0;
    for (Offset mayBeStackLimit = stackInfo._stackLimit;
         mayBeStackLimit > stackInfo._stackBase; mayBeStackLimit -= 0x1000) {
      Offset mayBeReturn =
          reader.ReadOffset(mayBeStackLimit - sizeof(Offset), 0);
      if (_acceptedReturnAddresses.find(mayBeReturn) !=
          _acceptedReturnAddresses.end()) {
        stackInfo._stackLimit = mayBeStackLimit;
        stackInfo._isResolved = true;
        return true;
      }
      typename VirtualAddressMap<Offset>::const_iterator it =
          _virtualAddressMap.find(mayBeReturn);
      if (it == _virtualAddressMap.end()) {
        continue;
      }
      if ((it.Flags() & RangeAttributes::IS_EXECUTABLE) == 0) {
        continue;
      }
      if (++numNewReturnsFound == 1) {
        stackInfo._stackLimit = mayBeStackLimit;
        possibleNewReturn = mayBeReturn;
      }
    }

    if (numNewReturnsFound != 1) {
      return false;
    }

    stackInfo._isResolved = true;
    _acceptedReturnAddresses.insert(possibleNewReturn);
    return true;
  }

  void ResolveStackLimits() {
    size_t stackIndex = 0;
    for (StackInfo& stackInfo : _stacks) {
      if (!ResolveStackLimit(stackInfo)) {
        _unresolvedStackIndices.push_back(stackIndex);
      }
    }
    size_t numUnresolved = _unresolvedStackIndices.size();
    if (numUnresolved == 0) {
      return;
    }
    if (numUnresolved == _stacks.size()) {
      std::cerr << "The current algorithm fails to resolve sizes of folly "
                   "fiber stacks.\n";
      return;
    }
    size_t numUnresolvedOnSecondPass = 0;
    for (size_t i = 0; i < numUnresolved; i++) {
      if (!ResolveStackLimit(_stacks[_unresolvedStackIndices[i]])) {
        if (numUnresolvedOnSecondPass != i) {
          _unresolvedStackIndices[numUnresolvedOnSecondPass] =
              _unresolvedStackIndices[i];
        }
        numUnresolvedOnSecondPass++;
      }
    }
    if (numUnresolvedOnSecondPass != numUnresolved) {
      numUnresolved = numUnresolvedOnSecondPass;
      _unresolvedStackIndices.resize(numUnresolved);
    }
  }

  void RegisterStacks() {
    for (const StackInfo& stackInfo : _stacks) {
      if (!stackInfo._isResolved) {
        continue;
      }
      Offset guardBase = stackInfo._guardBase;
      Offset stackBase = stackInfo._stackBase;
      Offset stackLimit = stackInfo._stackLimit;
      if (!_virtualMemoryPartition.ClaimRange(stackBase, stackLimit - stackBase,
                                              FOLLY_FIBER_STACK, false)) {
        std::cerr << "Warning: Failed to claim address range for "
                  << FOLLY_FIBER_STACK << " [0x" << std::hex << stackBase
                  << ", 0x" << stackLimit
                  << ") due to overlap with another address range.\n";
      }
      if (!_stackRegistry.RegisterStack(stackBase, stackLimit,
                                        FOLLY_FIBER_STACK)) {
        std::cerr << "Warning: Failed to register " << FOLLY_FIBER_STACK
                  << " [0x" << std::hex << stackBase << ", 0x" << stackLimit
                  << ") due to overlap with another stack.\n";
      }
      if (!_virtualMemoryPartition.ClaimRange(guardBase, stackBase - guardBase,
                                              FOLLY_FIBER_STACK_OVERFLOW_GUARD,
                                              false)) {
        std::cerr << "Warning: Failed to claim "
                  << FOLLY_FIBER_STACK_OVERFLOW_GUARD << " [0x" << std::hex
                  << guardBase << ", 0x" << stackBase
                  << ") due to overlap with another address range.\n";
      }
    }
  }

  bool FindAndRegisterStacks(
      const typename ModuleDirectory<Offset>::ModuleInfo& moduleInfo) {
    if (FindStacks(moduleInfo)) {
      ResolveStackLimits();
      RegisterStacks();
      return true;
    }
    return false;
  }
};
}  // namespace FollyFibers
}  // namespace chap
