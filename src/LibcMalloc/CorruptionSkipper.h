// Copyright (c) 2017-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace LibcMalloc {
template <class Offset>
class CorruptionSkipper {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  CorruptionSkipper(const VirtualAddressMap<Offset>& addressMap,
                    const InfrastructureFinder<Offset>& infrastructureFinder)
      : _addressMap(addressMap),
        _arenas(infrastructureFinder.GetArenas()),
        _mainArenaAddress(infrastructureFinder.GetMainArenaAddress()),
        _fastBinStartOffset(infrastructureFinder.GetFastBinStartOffset()),
        _fastBinLimitOffset(infrastructureFinder.GetFastBinLimitOffset()),
        _arenaDoublyLinkedFreeListOffset(
            infrastructureFinder.GetArenaDoublyLinkedFreeListOffset()) {}

  Offset FindBackChain(Offset libcChunkStart, Offset corruptionPoint) {
    Offset lowestChainStart = libcChunkStart;
    Reader reader(_addressMap);
    Offset sizeCheckMask = (sizeof(Offset) == 8) ? 0xa : 2;
    for (Offset check = libcChunkStart - 4 * sizeof(Offset);
         check > corruptionPoint; check -= (2 * sizeof(Offset))) {
      Offset sizeAndStatus = reader.ReadOffset(check + sizeof(Offset), 2);
      if ((sizeAndStatus & sizeCheckMask) != 0) {
        continue;
      }
      Offset length = sizeAndStatus & ~7;
      if (length == 0 || length > (libcChunkStart = check)) {
        continue;
      }
      if ((sizeAndStatus & 1) == 0) {
        Offset prevSizeAndStatus = reader.ReadOffset(check, 2);
        if ((prevSizeAndStatus & sizeCheckMask) != 0) {
          continue;
        }
        Offset prevLength = prevSizeAndStatus & ~7;
        if (check - corruptionPoint <= prevLength) {
          continue;
        }
        if ((reader.ReadOffset(check - prevLength, 0) & ~7) != prevLength) {
          continue;
        }
      }

      if (check + length == lowestChainStart) {
        lowestChainStart = check;
      } else {
        Offset checkForward = check + length;
        Offset prevLength = length;
        while (checkForward != libcChunkStart) {
          Offset forwardSizeAndStatus =
              reader.ReadOffset(checkForward + sizeof(Offset), 2);
          if ((forwardSizeAndStatus & sizeCheckMask) != 0) {
            break;
          }
          if ((forwardSizeAndStatus & 1) == 0 &&
              (reader.ReadOffset(checkForward, 0) & ~7) != prevLength) {
            break;
          }
          Offset forwardLength = forwardSizeAndStatus & ~7;
          if (forwardLength == 0 ||
              forwardLength > (libcChunkStart - checkForward)) {
            break;
          }
          prevLength = forwardLength;
          checkForward += forwardLength;
        }
        if (checkForward == libcChunkStart) {
          lowestChainStart = check;
        }
      }
    }
    return lowestChainStart;
  }

  Offset SkipArenaCorruption(Offset arenaAddress, Offset corruptionPoint,
                             Offset repairLimit) {
    const typename InfrastructureFinder<Offset>::ArenaMap::const_iterator it =
        _arenas.find(arenaAddress);
    if (it == _arenas.end() || it->second._missingOrUnfilledHeader) {
      return 0;
    }
    Offset pastArenaCorruption = 0;
    Offset top = it->second._top;
    if (corruptionPoint == top) {
      return 0;
    }
    if (corruptionPoint < top && top <= repairLimit) {
      repairLimit = top;
    } else {
      repairLimit -= 6 * sizeof(Offset);
    }

    Offset expectClearMask = 2;
    if (arenaAddress == _mainArenaAddress) {
      expectClearMask = expectClearMask | 4;
    }
    if (sizeof(Offset) == 8) {
      expectClearMask = expectClearMask | 8;
    }
    Reader reader(_addressMap);
    Offset fastBinLimit = arenaAddress + _fastBinLimitOffset;
    for (Offset fastBinCheck = arenaAddress + _fastBinStartOffset;
         fastBinCheck < fastBinLimit; fastBinCheck += sizeof(Offset)) {
      int loopGuard = 0;
      for (Offset listNode = reader.ReadOffset(fastBinCheck); listNode != 0;
           listNode = reader.ReadOffset(listNode + 2 * sizeof(Offset), 0)) {
        if (++loopGuard == 10000000) {
          break;
        }
        if (listNode > corruptionPoint && listNode <= repairLimit) {
          Offset sizeAndFlags = reader.ReadOffset(listNode + sizeof(Offset), 0);
          if (sizeAndFlags != 0 && ((sizeAndFlags & expectClearMask) == 0) &&
              ((listNode + (sizeAndFlags & ~7)) <= repairLimit)) {
            if (pastArenaCorruption == 0 || listNode < pastArenaCorruption) {
              pastArenaCorruption = listNode;
            }
          }
        }
      }
    }
    for (Offset listHeader = arenaAddress + _arenaDoublyLinkedFreeListOffset -
                             2 * sizeof(Offset);
         ; listHeader += (2 * sizeof(Offset))) {
      Offset listNode = reader.ReadOffset(listHeader + 2 * sizeof(Offset), 0);
      if (listNode == listHeader) {
        // The list was empty.
        continue;
      }
      if (reader.ReadOffset(listNode + 3 * sizeof(Offset), 0) != listHeader) {
        break;
      }
      do {
        if (listNode > corruptionPoint && listNode <= repairLimit) {
          Offset sizeAndFlags = reader.ReadOffset(listNode + sizeof(Offset), 0);
          if (sizeAndFlags != 0 && ((sizeAndFlags & expectClearMask) == 0) &&
              ((listNode + (sizeAndFlags & ~7)) <= repairLimit)) {
            if (pastArenaCorruption == 0 || listNode < pastArenaCorruption) {
              pastArenaCorruption = listNode;
            }
          }
        }
        Offset nextNode = reader.ReadOffset(listNode + 2 * sizeof(Offset), 0);
        if (nextNode != 0 ||
            reader.ReadOffset(nextNode + 3 * sizeof(Offset), 0) != listNode) {
          /*
           * We reached a break in the list, most likely due to corruption
           * but possibly due to a zero-filled part of a heap given that we
           * attempt to extract what we can from such incomplete cores.
           */
          break;
        }
        listNode = nextNode;
      } while (listNode != listHeader);
    }
    if (pastArenaCorruption == 0) {
      if (repairLimit == top && top > corruptionPoint) {
        pastArenaCorruption = FindBackChain(top, corruptionPoint);
      }
    } else {
      pastArenaCorruption = FindBackChain(pastArenaCorruption, corruptionPoint);
    }
    return pastArenaCorruption;
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
  const typename InfrastructureFinder<Offset>::ArenaMap _arenas;
  const Offset _mainArenaAddress;
  const Offset _fastBinStartOffset;
  const Offset _fastBinLimitOffset;
  const Offset _arenaDoublyLinkedFreeListOffset;
};

}  // namespace LibcMalloc
}  // namespace chap
