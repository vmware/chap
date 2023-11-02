// Copyright (c) 2021,2023 VMware, Inc. All Rights Reserved.
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
        _listInPThread(DEFAULT_LIST_OFFSET_IN_PTHREAD),
        _stackBlockInPThread(DEFAULT_STACK_BLOCK_OFFSET_IN_PTHREAD),
        _stackBlockSizeInPThread(DEFAULT_STACK_BLOCK_OFFSET_IN_PTHREAD +
                                 sizeof(Offset)),
        _stackGuardSizeInPThread(DEFAULT_STACK_BLOCK_OFFSET_IN_PTHREAD +
                                 2 * sizeof(Offset)),
        _virtualMemoryPartition(partition),
        _stackRegistry(stackRegistry),
        _virtualAddressMap(partition.GetAddressMap()) {}

  const char* USED_PTHREAD_STACK;
  const char* CACHED_PTHREAD_STACK;
  const char* PTHREAD_STACK_OVERFLOW_GUARD;

  void Resolve() {
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
      if ((it->first.find("ld-linux") == std::string::npos)) {
        continue;
      }
      _pthreadLibraryPresent = true;
      FindStacks(it->second);
      if (_stacksFound) {
        _isResolved = true;
        return;
      }
    }
    it = _moduleDirectory.begin();
    for (; it != _moduleDirectory.end(); ++it) {
      if ((it->first.find("pthread") == std::string::npos)) {
        continue;
      }
      _pthreadLibraryPresent = true;
      FindStacks(it->second);
      if (_stacksFound) {
        _isResolved = true;
        return;
      }
    }
    for (; it != _moduleDirectory.end(); ++it) {
      if (it->first.find(".so") != std::string::npos) {
        continue;
      }
      FindStacks(it->second);
      if (_stacksFound) {
        _isResolved = true;
        return;
      }
    }
    if (_pthreadLibraryPresent) {
      std::cerr << "Warning: a pthread library appears to be in use but the "
                   "pthread stack lists were not found.\n";
    }
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }

 private:
  static constexpr Offset DEFAULT_LIST_OFFSET_IN_PTHREAD =
      (sizeof(Offset) == 4) ? 0x60 : 0x2c0;
  static constexpr Offset DEFAULT_LWP_OFFSET_IN_PTHREAD =
      (sizeof(Offset) == 4) ? 0x6c : 0x2d0;
  static constexpr Offset DEFAULT_STACK_BLOCK_OFFSET_IN_PTHREAD =
      (sizeof(Offset) == 4) ? 0x270 : 0x690;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  bool _isResolved;
  bool _pthreadLibraryPresent;
  bool _stacksFound;
  Offset _listInPThread;
  Offset _stackBlockInPThread;
  Offset _stackBlockSizeInPThread;
  Offset _stackGuardSizeInPThread;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  StackRegistry<Offset>& _stackRegistry;
  const VirtualAddressMap<Offset>& _virtualAddressMap;

  void RegisterStackAndClaimStackRange(Offset linkInChain, Reader& reader) {
    Offset pThreadAddr = linkInChain - _listInPThread;
    const char* stackType =
        (reader.Read32(pThreadAddr + DEFAULT_LWP_OFFSET_IN_PTHREAD, 0) > 0)
            ? USED_PTHREAD_STACK
            : CACHED_PTHREAD_STACK;
    Offset stackBlockAddr =
        reader.ReadOffset(pThreadAddr + _stackBlockInPThread, 0);
    if (stackBlockAddr == 0) {
      return;
    }
    Offset stackBlockSize =
        reader.ReadOffset(pThreadAddr + _stackBlockSizeInPThread, 0);
    if (stackBlockSize == 0) {
      return;
    }
    Offset stackGuardSize =
        reader.ReadOffset(pThreadAddr + _stackGuardSizeInPThread, 0xbad);
    if (((stackGuardSize & 0xfff) != 0) || (stackGuardSize >= stackBlockSize)) {
      return;
    }
    Offset base = stackBlockAddr + stackGuardSize;
    typename VirtualAddressMap<Offset>::const_iterator it =
        _virtualAddressMap.find(base);
    if (it == _virtualAddressMap.end()) {
      std::cerr << "Process image does not contain mapping for " << stackType
                << " that contains address 0x" << std::hex << base << "\n";
      return;
    }
    if (it.GetImage() == (const char*)(0)) {
      std::cerr << "Process image does not contain image for " << stackType
                << " that contains address 0x" << std::hex << base << "\n";
      return;
    }
    Offset limit = (linkInChain + 0xfff) & ~0xfff;
    if (!_virtualMemoryPartition.ClaimRange(base, limit - base, stackType,
                                            false)) {
      std::cerr << "Warning: Failed to claim " << stackType << " [" << std::hex
                << base << ", " << limit << ") due to overlap.\n";
    }
    if (_stackRegistry.RegisterStack(base, limit, stackType)) {
      if ((stackGuardSize != 0) &&
          !_virtualMemoryPartition.ClaimRange(stackBlockAddr, stackGuardSize,
                                              PTHREAD_STACK_OVERFLOW_GUARD,
                                              false)) {
        std::cerr << "Warning: Failed to claim " << PTHREAD_STACK_OVERFLOW_GUARD
                  << " [" << std::hex << stackBlockAddr << ", " << base
                  << ") due to overlap.\n";
      }
    } else {
      std::cerr << "Warning: Failed to register " << stackType << " ["
                << std::hex << base << ", " << limit
                << ") due to overlap with other stack.\n";
    }
  }

  bool RegisterStacks(Reader& reader, Offset listHeader, Offset firstInChain) {
    for (Offset linkInChain = firstInChain; linkInChain != listHeader;
         linkInChain = reader.ReadOffset(linkInChain, 0xbad)) {
      if ((linkInChain & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      RegisterStackAndClaimStackRange(linkInChain, reader);
    }
    return true;
  }

  bool RegisterStacksBackwards(Reader& reader, Offset listHeader,
                               Offset lastInChain) {
    for (Offset linkInChain = lastInChain; linkInChain != listHeader;
         linkInChain = reader.ReadOffset(linkInChain + sizeof(Offset), 0xbad)) {
      if ((linkInChain & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      RegisterStackAndClaimStackRange(linkInChain, reader);
    }
    return true;
  }

  bool IsPlausibleNonEmptyPThreadList(Reader& moduleReader, Reader& reader,
                                      Offset listHead) {
    Offset listStartCandidate = moduleReader.ReadOffset(listHead, 0xbad);
    if (listStartCandidate == listHead) {
      return false;
    }
    if ((listStartCandidate & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    Offset listEndCandidate =
        moduleReader.ReadOffset(listHead + sizeof(Offset), 0xbad);
    if ((listEndCandidate & (sizeof(Offset) - 1)) != 0) {
      return false;
    }

    if (reader.ReadOffset(listStartCandidate + sizeof(Offset), 0xbad) !=
        listHead) {
      return false;
    }

    if (reader.ReadOffset(listStartCandidate + 4 * sizeof(Offset), 0xbad) !=
        listStartCandidate + 4 * sizeof(Offset)) {
      return false;
    }

    if (reader.ReadOffset(listEndCandidate, 0xbad) != listHead) {
      return false;
    }

    if (reader.ReadOffset(listEndCandidate + 4 * sizeof(Offset), 0xbad) !=
        listEndCandidate + 4 * sizeof(Offset)) {
      return false;
    }

    _stacksFound = true;
    return true;
  }

  void FindStacks(
      const typename ModuleDirectory<Offset>::ModuleInfo& moduleInfo) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    std::vector<Offset> listHeads;
    for (const auto& range : moduleInfo._ranges) {
      int flags = range._value._flags;
      if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      Offset base = range._base;
      Offset limit = range._limit - 3 * sizeof(Offset);

      for (Offset moduleAddr = base; moduleAddr < limit;
           moduleAddr += sizeof(Offset)) {
        if (IsPlausibleNonEmptyPThreadList(moduleReader, reader, moduleAddr)) {
          listHeads.push_back(moduleAddr);
          moduleAddr += sizeof(Offset);
        }
      }
    }

    if (listHeads.empty()) {
      // Apparently, there are no PThreads to register.
      return;
    }

    bool stackBlockOffsetChecked = false;
    for (Offset listHead : listHeads) {
      Offset firstListFieldAddr = moduleReader.ReadOffset(listHead, 0xbad);
      Offset firstPThreadAddr = firstListFieldAddr - _listInPThread;
      if (reader.ReadOffset(firstPThreadAddr, 0xbadbad) != firstPThreadAddr) {
        std::cerr << "Warning: assumption about list field in pthread is "
                     "wrong.  Please report this.\n";
        return;
      }
      for (Offset listFieldAddr = firstListFieldAddr;
           (listFieldAddr != listHead) && (listFieldAddr != 0xbad);
           listFieldAddr = reader.ReadOffset(listFieldAddr, 0xbad)) {
        Offset pThreadAddr = listFieldAddr - _listInPThread;
        Offset stackBlock =
            reader.ReadOffset(pThreadAddr + _stackBlockInPThread, 0);
        if (stackBlock == 0 || pThreadAddr <= stackBlock) {
          continue;
        }

        Offset stackBlockSize =
            reader.ReadOffset(pThreadAddr + _stackBlockSizeInPThread, 0);
        if (stackBlock + stackBlockSize <=
            pThreadAddr + _stackGuardSizeInPThread) {
          continue;
        }
        Offset stackGuardSize =
            reader.ReadOffset(pThreadAddr + _stackGuardSizeInPThread, 0xbad);
        if (((stackGuardSize & 0xfff) != 0) ||
            stackGuardSize >= stackBlockSize) {
          std::cerr
              << "Warning: assumption about stack guard field in pthread is "
                 "wrong.  Please report this.\n";
          return;
        }
        stackBlockOffsetChecked = true;
      }
    }

    if (!stackBlockOffsetChecked) {
      return;
    }

    for (Offset listHead : listHeads) {
      if (!RegisterStacks(reader, listHead,
                          moduleReader.ReadOffset(listHead, 0xbad))) {
        (void)RegisterStacksBackwards(
            reader, listHead,
            moduleReader.ReadOffset(listHead + sizeof(Offset), 0xbad));
      }
    }
  }
};
}  // namespace PThread
}  // namespace chap
