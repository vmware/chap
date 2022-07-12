// Copyright (c) 2017-2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include "../Allocations/Directory.h"
#include "../ThreadMap.h"
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace LibcMalloc {
template <class Offset>
class FastBinFreeStatusFixer {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename InfrastructureFinder<Offset>::Arena Arena;

  FastBinFreeStatusFixer(
      const VirtualAddressMap<Offset>& addressMap,
      const InfrastructureFinder<Offset>& infrastructureFinder,
      Allocations::Directory<Offset>& allocationDirectory,
      const ThreadMap<Offset>& threadMap)
      : _addressMap(addressMap),
        _infrastructureFinder(infrastructureFinder),
        _maxHeapSize(infrastructureFinder.GetMaxHeapSize()),
        _fastBinStartOffset(infrastructureFinder.GetFastBinStartOffset()),
        _fastBinLimitOffset(infrastructureFinder.GetFastBinLimitOffset()),
        _fastBinLinksAreMangled(infrastructureFinder.FastBinLinksAreMangled()),
        _allocationDirectory(allocationDirectory),
        _threadMap(threadMap) {}

  void MarkFastBinItemsAsFree(const Arena& arena, bool isMainArena,
                              const size_t finderIndex) {
    bool corruptionReported = false;
    Offset arenaAddress = arena._address;
    Offset fastBinStart = arenaAddress + _fastBinStartOffset;
    Offset fastBinLimit = arenaAddress + _fastBinLimitOffset;
    Reader reader(_addressMap);
    AllocationIndex numAllocations = _allocationDirectory.NumAllocations();
    for (Offset fastBinCheck = fastBinStart; fastBinCheck < fastBinLimit;
         fastBinCheck += sizeof(Offset)) {
      try {
        AllocationIndex numIndicesVisited = 0;
        Offset nextNode = reader.ReadOffset(fastBinCheck);
        while (nextNode != 0) {
          if (++numIndicesVisited > numAllocations) {
            ReportFastBinCycle(arena, fastBinCheck, corruptionReported,
                               numAllocations);
            break;
          }
          Offset allocation = nextNode + sizeof(Offset) * 2;
          AllocationIndex index =
              _allocationDirectory.AllocationIndexOf(allocation);
          if (index == numAllocations ||
              _allocationDirectory.AllocationAt(index)->Address() !=
                  allocation) {
            ReportFastBinCorruption(arena, fastBinCheck, nextNode,
                                    "not matching an allocation",
                                    corruptionReported);
            // It is not possible to process the rest of this
            // fast bin list because there is a break in the
            // chain.
            // TODO: A possible improvement would be to try
            // to recognize any orphan fast bin lists.  Doing
            // so here would be the best place because if we
            // fail to find the rest of the fast bin list, which
            // in rare cases can be huge, the used/free status
            // will be wrong for remaining entries on that
            // particular fast bin list.
            break;
          }
          if (_infrastructureFinder.ArenaAddressFor(nextNode) != arenaAddress) {
            ReportFastBinCorruption(arena, fastBinCheck, nextNode,
                                    "in the wrong arena", corruptionReported);
            // It is not possible to process the rest of this
            // fast bin list because there is a break in the
            // chain.
            // TODO: A possible improvement would be to try
            // to recognize any orphan fast bin lists.  Doing
            // so here would be the best place because if we
            // fail to find the rest of the fast bin list, which
            // in rare cases can be huge, the used/free status
            // will be wrong for remaining entries on that
            // particular fast bin list.
            break;
          }
          _allocationDirectory.MarkAsFree(index);
          nextNode = reader.ReadOffset(allocation);
          if (_fastBinLinksAreMangled) {
            nextNode = nextNode ^ (allocation >> 12);
          }
        }
      } catch (typename VirtualAddressMap<Offset>::NotMapped& e) {
        // It is not possible to process the rest of this
        // fast bin list because there is a break in the
        // chain.
        // TODO: A possible improvement would be to try
        // to recognize any orphan fast bin lists.  Doing
        // so here would be the best place because if we
        // fail to find the rest of the fast bin list, which
        // in rare cases can be huge, the used/free status
        // will be wrong for remaining entries on that
        // particular fast bin list.
        if (e._address == fastBinCheck) {
          std::cerr << "The arena header at 0x" << std::hex << arenaAddress
                    << " is not in the core.\n";
          return;
        }
        ReportFastBinCorruption(arena, fastBinCheck, e._address,
                                "not in the core", corruptionReported);
      }
    }
    if ((reader.ReadOffset(arenaAddress, 0) & 1) != 0) {
      size_t maxChainLength = 0;
      Offset longestChainStart = 0;
      Offset stopMarkingLongestChainAt = 0;
      size_t numRegisters = _threadMap.GetNumRegisters();
      for (const auto threadInfo : _threadMap) {
        Offset* registers = threadInfo._registers;
        bool hasRegisterReferenceToFastBinHeader = false;
        for (size_t i = 0; i < numRegisters; i++) {
          Offset regVal = registers[i];
          if (regVal >= fastBinStart && regVal < fastBinLimit &&
              (regVal & (sizeof(Offset) - 1)) == 0) {
            hasRegisterReferenceToFastBinHeader = true;
          }
        }
        if (!hasRegisterReferenceToFastBinHeader) {
          continue;
        }
        for (size_t i = 0; i < numRegisters; i++) {
          Offset regValue = registers[i];
          if (regValue >= arenaAddress && regValue < fastBinLimit) {
            continue;
          }
          if ((regValue & (2 * sizeof(Offset) - 1)) != 0) {
            continue;
          }
          Offset lengthAndStatus =
              reader.ReadOffset(regValue + sizeof(Offset), 0xff);
          if (isMainArena) {
            if ((lengthAndStatus & 6) != 0) {
              continue;
            }
          } else {
            if ((lengthAndStatus & 6) != 4) {
              continue;
            }
            Offset heap = regValue & ~(_maxHeapSize - 1);
            if (reader.ReadOffset(heap, 0xbad) != arenaAddress) {
              continue;
            }
          }
          Offset allocationAddress = regValue + 2 * sizeof(Offset);
          AllocationIndex index =
              _allocationDirectory.AllocationIndexOf(allocationAddress);
          if (index == numAllocations) {
            continue;
          }
          const typename Allocations::Directory<Offset>::Allocation*
              allocation = _allocationDirectory.AllocationAt(index);
          if (allocation->Address() != allocationAddress ||
              allocation->FinderIndex() != finderIndex ||
              !(allocation->IsUsed())) {
            continue;
          }
          size_t chainLength = 1;
          Offset link = reader.ReadOffset(allocationAddress, 0xbad);
          if (_fastBinLinksAreMangled) {
            link = link ^ (allocationAddress >> 12);
          }
          while (link != 0 && link != 0xbad) {
            allocationAddress = link + 2 * sizeof(Offset);
            index = _allocationDirectory.AllocationIndexOf(allocationAddress);
            if (index == numAllocations) {
              break;
            }
            allocation = _allocationDirectory.AllocationAt(index);
            if (allocation->Address() != allocationAddress ||
                allocation->FinderIndex() != finderIndex ||
                !(allocation->IsUsed())) {
              break;
            }
            chainLength++;
            if (chainLength == 0x10000000) {
              chainLength = 0;
              std::cerr << "Warning: A possible cyclic consolodating fast bin "
                           "chain at 0x"
                        << std::hex << regValue
                        << " was found\n...for libc malloc arena 0x"
                        << arenaAddress << ".\n";
            }
            link = reader.ReadOffset(allocationAddress, 0xbad);
            if (_fastBinLinksAreMangled) {
              link = link ^ (allocationAddress >> 12);
            }
          }

          if (chainLength > maxChainLength) {
            maxChainLength = chainLength;
            longestChainStart = regValue;
            stopMarkingLongestChainAt = link;
          }
        }
      }
      if (maxChainLength > 0) {
        Offset lastValidLink = 0;
        for (Offset link = longestChainStart;
             link != stopMarkingLongestChainAt;) {
          lastValidLink = link;
          Offset allocationAddress = link + 2 * sizeof(Offset);
          _allocationDirectory.MarkAsFree(
              _allocationDirectory.AllocationIndexOf(allocationAddress));
          link = reader.ReadOffset(allocationAddress, 0xbad);
          if (_fastBinLinksAreMangled) {
            link = link ^ (allocationAddress >> 12);
          }
        }
        if (stopMarkingLongestChainAt != 0) {
          std::cerr
              << "Warning: An incomplete consolodating fast bin chain at 0x"
              << std::hex << longestChainStart
              << " was found\n...for libc malloc arena 0x" << arenaAddress
              << ".\n...The last valid link was 0x" << lastValidLink << "\n";
        }
      }
    }
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const Offset _maxHeapSize;
  const Offset _fastBinStartOffset;
  const Offset _fastBinLimitOffset;
  const bool _fastBinLinksAreMangled;
  Allocations::Directory<Offset>& _allocationDirectory;
  const ThreadMap<Offset>& _threadMap;
  void ReportFastBinCorruption(const Arena& arena, Offset fastBinHeader,
                               Offset node, const char* specificError,
                               bool& corruptionReported) {
    if (!corruptionReported) {
      corruptionReported = true;
      std::cerr << "Fast bin corruption was found for the arena"
                   " at 0x"
                << std::hex << arena._address << "\n";
      std::cerr << "  Leak analysis will not be accurate.\n";
      std::cerr << "  Used/free analysis will not be accurate "
                   "for the arena.\n";
    }
    std::cerr << "  The fast bin list headed at 0x" << std::hex << fastBinHeader
              << " has a node\n  0x" << node << " " << specificError << ".\n";
  }

  void ReportFastBinCycle(const Arena& arena, Offset fastBinHeader,
                          bool& corruptionReported,
                          AllocationIndex numAllocations) {
    Reader reader(_addressMap);
    std::vector<bool> alreadySeen;
    alreadySeen.resize(numAllocations, false);
    Offset nextNode = reader.ReadOffset(fastBinHeader);
    while (nextNode != 0) {
      Offset allocation = nextNode + sizeof(Offset) * 2;
      AllocationIndex index =
          _allocationDirectory.AllocationIndexOf(allocation);
      if (alreadySeen[index]) {
        ReportFastBinCorruption(
            arena, fastBinHeader, nextNode,
            "involved in a cycle, probably due to a double free",
            corruptionReported);
        break;
      }
      alreadySeen[index] = true;
      nextNode = reader.ReadOffset(nextNode + sizeof(Offset) * 2);
      if (_fastBinLinksAreMangled) {
        nextNode = nextNode ^ (allocation >> 12);
      }
    }
  }
};

}  // namespace LibcMalloc
}  // namespace chap
