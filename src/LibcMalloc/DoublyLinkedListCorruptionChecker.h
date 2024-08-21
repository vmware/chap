// Copyright (c) 2017-2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include "../Allocations/Directory.h"
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace LibcMalloc {
template <class Offset>
class DoublyLinkedListCorruptionChecker {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  DoublyLinkedListCorruptionChecker(
      const VirtualAddressMap<Offset>& addressMap,
      const InfrastructureFinder<Offset>& infrastructureFinder,
      const Allocations::Directory<Offset>& allocationDirectory)
      : _addressMap(addressMap),
        _infrastructureFinder(infrastructureFinder),
        _arenaDoublyLinkedFreeListOffset(
            infrastructureFinder.GetArenaDoublyLinkedFreeListOffset()),
        _arenaLastDoublyLinkedFreeListOffset(
            infrastructureFinder.GetArenaLastDoublyLinkedFreeListOffset()),
        _allocationDirectory(allocationDirectory) {}

  void CheckDoublyLinkedListCorruption(
      const typename InfrastructureFinder<Offset>::Arena& arena) {
    if (arena._missingOrUnfilledHeader) {
      return;
    }
    bool corruptionReported = false;
    typename Allocations::Directory<Offset>::AllocationIndex noAllocation =
        _allocationDirectory.NumAllocations();
    typename VirtualAddressMap<Offset>::Reader reader(_addressMap);
    Offset arenaAddress = arena._address;
    Offset firstList =
        arenaAddress + _arenaDoublyLinkedFreeListOffset - 2 * sizeof(Offset);
    Offset lastList = arenaAddress + _arenaLastDoublyLinkedFreeListOffset -
                      2 * sizeof(Offset);
    for (Offset list = firstList; list <= lastList;
         list += 2 * sizeof(Offset)) {
      try {
        Offset firstNode = reader.ReadOffset(list + 2 * sizeof(Offset));
        Offset lastNode = reader.ReadOffset(list + 3 * sizeof(Offset));
        if (firstNode == list) {
          if (lastNode != list) {
            ReportFreeListCorruption(arena, list + 2 * sizeof(Offset), lastNode,
                                     "at end of list with empty start",
                                     corruptionReported);
          }
        } else {
          if (lastNode == list) {
            ReportFreeListCorruption(arena, list + 2 * sizeof(Offset), lastNode,
                                     "at start of list with empty end",
                                     corruptionReported);
          } else {
            Offset prevNode = list;
            for (Offset node = firstNode; node != list;
                 node = reader.ReadOffset(node + 2 * sizeof(Offset))) {
              Offset allocationAddr = node + 2 * sizeof(Offset);
              typename Allocations::Directory<Offset>::AllocationIndex index =
                  _allocationDirectory.AllocationIndexOf(allocationAddr);
              if (index == noAllocation) {
                ReportFreeListCorruption(arena, list + 2 * sizeof(Offset), node,
                                         "not matching an allocation",
                                         corruptionReported);
                break;
              }
              const typename Allocations::Directory<Offset>::Allocation*
                  allocation = _allocationDirectory.AllocationAt(index);
              if (allocation->Address() != allocationAddr) {
                if (prevNode == list) {
                  ReportFreeListCorruption(
                      arena, list + 2 * sizeof(Offset), node,
                      "with wrong offset from allocation", corruptionReported);
                } else {
                  ReportFreeListCorruption(
                      arena, list + 2 * sizeof(Offset), prevNode,
                      "with an unexpected forward link", corruptionReported);
                }
                break;
              }
              Offset allocationSize = allocation->Size();
              if ((reader.ReadOffset(allocationAddr + allocationSize) & 1) !=
                  0) {
                ReportFreeListCorruption(arena, list + 2 * sizeof(Offset), node,
                                         "with a wrong used/free status bit",
                                         corruptionReported);
                break;
              }
              if (_infrastructureFinder.ArenaAddressFor(node) != arenaAddress) {
                ReportFreeListCorruption(arena, list + 2 * sizeof(Offset), node,
                                         "in the wrong arena",
                                         corruptionReported);
                break;
              }
              if (reader.ReadOffset(node + 3 * sizeof(Offset)) != prevNode) {
                ReportFreeListCorruption(arena, list + 2 * sizeof(Offset), node,
                                         "with an unexpected back pointer",
                                         corruptionReported);
                break;
              }
              if (reader.ReadOffset(allocationAddr + allocationSize -
                                    sizeof(Offset)) !=
                  allocationSize + sizeof(Offset)) {
                ReportFreeListCorruption(arena, list + 2 * sizeof(Offset), node,
                                         "with a wrong prev size at end",
                                         corruptionReported);
                break;
              }
              prevNode = node;
            }
          }
        }
      } catch (const typename VirtualAddressMap<Offset>::NotMapped& e) {
        ReportFreeListCorruption(arena, list + 2 * sizeof(Offset), e._address,
                                 "not in the core", corruptionReported);
      }
    }
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const Offset _arenaDoublyLinkedFreeListOffset;
  const Offset _arenaLastDoublyLinkedFreeListOffset;
  const Allocations::Directory<Offset>& _allocationDirectory;
  void ReportFreeListCorruption(
      const typename InfrastructureFinder<Offset>::Arena& arena,
      Offset freeListHeader, Offset node, const char* specificError,
      bool& corruptionReported) {
    if (!corruptionReported) {
      corruptionReported = true;
      std::cerr << "Doubly linked free list corruption was "
                   "found for the arena"
                   " at 0x"
                << std::hex << arena._address << "\n";
      std::cerr << "  Leak analysis may not be accurate.\n";
      /*
       * Unlike the fast bin case, the chunks on the doubly linked free
       * lists are actually marked as free, so a cut in a doubly linked
       * list will not compromise the understanding of whether the remaining
       * nodes on the list are free or not.
       */
    }
    std::cerr << "  The free list headed at 0x" << std::hex << freeListHeader
              << " has a node\n  0x" << node << " " << specificError << ".\n";
  }
};

}  // namespace LibcMalloc
}  // namespace chap
