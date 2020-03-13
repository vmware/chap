// Copyright (c) 2017-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include "../Allocations/Directory.h"
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace LibcMalloc {
template <class Offset>
class FastBinFreeStatusFixer {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  FastBinFreeStatusFixer(
      const VirtualAddressMap<Offset>& addressMap,
      const InfrastructureFinder<Offset>& infrastructureFinder,
      Allocations::Directory<Offset>& allocationDirectory)
      : _addressMap(addressMap),
        _infrastructureFinder(infrastructureFinder),
        _fastBinStartOffset(infrastructureFinder.GetFastBinStartOffset()),
        _fastBinLimitOffset(infrastructureFinder.GetFastBinLimitOffset()),
        _allocationDirectory(allocationDirectory) {}

  void MarkFastBinItemsAsFree(
      const typename InfrastructureFinder<Offset>::Arena& arena) {
    bool corruptionReported = false;
    Offset arenaAddress = arena._address;
    Offset fastBinLimit = arenaAddress + _fastBinLimitOffset;
    typename VirtualAddressMap<Offset>::Reader reader(_addressMap);
    for (Offset fastBinCheck = arenaAddress + _fastBinStartOffset;
         fastBinCheck < fastBinLimit; fastBinCheck += sizeof(Offset)) {
      try {
        for (Offset nextNode = reader.ReadOffset(fastBinCheck); nextNode != 0;
             nextNode = reader.ReadOffset(nextNode + sizeof(Offset) * 2)) {
          Offset allocation = nextNode + sizeof(Offset) * 2;
          typename Allocations::Directory<Offset>::AllocationIndex index =
              _allocationDirectory.AllocationIndexOf(allocation);
          if (index == _allocationDirectory.NumAllocations() ||
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
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const Offset _fastBinStartOffset;
  const Offset _fastBinLimitOffset;
  Allocations::Directory<Offset>& _allocationDirectory;
  void ReportFastBinCorruption(
      const typename InfrastructureFinder<Offset>::Arena& arena,
      Offset fastBinHeader, Offset node, const char* specificError,
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
};

}  // namespace LibcMalloc
}  // namespace chap
