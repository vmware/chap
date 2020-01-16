// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <algorithm>
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"

namespace chap {
namespace Python {
template <typename Offset>
class InfrastructureFinder {
 public:
  InfrastructureFinder(const ModuleDirectory<Offset>& moduleDirectory,
                       VirtualMemoryPartition<Offset>& partition)
      : PYTHON_ARENA("python arena"),
        _moduleDirectory(moduleDirectory),
        _libraryBase(0),
        _libraryLimit(0),
        _isResolved(false),
        _virtualMemoryPartition(partition),
        _virtualAddressMap(partition.GetAddressMap()),
        _arenaOffset(0),
        _poolsLimitOffset(_arenaOffset + sizeof(Offset)),
        _numFreePoolsOffset(_poolsLimitOffset + sizeof(Offset)),
        _maxPoolsOffset(_numFreePoolsOffset + sizeof(uint32_t)),
        _availablePoolsOffset(_maxPoolsOffset + sizeof(uint32_t)),
        _nextOffset(_availablePoolsOffset + sizeof(Offset)),
        _prevOffset(_nextOffset + sizeof(Offset)),
        _arenaStructSize(_prevOffset + sizeof(Offset)),
        _numArenas(0),
        _arenaStructArray(0),
        _arenaStructCount(0),
        _arenaStructArrayLimit(0),
        _arenaSize(0),
        _poolSize(0),
        _maxPoolsIfAligned(0),
        _maxPoolsIfNotAligned(0),
        _allArenasAreAligned(true)

  {}
  void Resolve() {
    if (_isResolved) {
      abort();
    }
    if (!_moduleDirectory.IsResolved()) {
      abort();
    }

    for (typename ModuleDirectory<Offset>::const_iterator it =
             _moduleDirectory.begin();
         it != _moduleDirectory.end(); ++it) {  // TB debug
      if (it->first.find("libpython") != std::string::npos) {
        _libraryPath = it->first;
        const typename ModuleDirectory<Offset>::RangeToFlags& rangeToFlags =
            it->second;
        _libraryBase = rangeToFlags.begin()->_base;
        _libraryLimit = rangeToFlags.rbegin()->_limit;
        FindArenaStructArray(rangeToFlags);
        break;
      }
    }
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }
  Offset ArenaStructFor(Offset candidateAddressInArena) const {
    if (_activeIndices.empty()) {
      return 0;
    }

    std::vector<uint32_t>::size_type leftToCheck = _activeIndices.size();
    const uint32_t* remainingToCheck = &(_activeIndices[0]);

    Reader reader(_virtualAddressMap);
    while (leftToCheck != 0) {
      std::vector<uint32_t>::size_type halfLeftToCheck = leftToCheck / 2;
      const uint32_t* nextToCheck = remainingToCheck + halfLeftToCheck;
      Offset arenaStruct =
          _arenaStructArray + ((*nextToCheck) * _arenaStructSize);
      Offset arena = reader.ReadOffset(arenaStruct);
      if ((arena + _arenaSize) <= candidateAddressInArena) {
        remainingToCheck = ++nextToCheck;
        leftToCheck -= (halfLeftToCheck + 1);
      } else {
        if (arena <= candidateAddressInArena) {
          return arenaStruct;
        }
        leftToCheck = halfLeftToCheck;
      }
    }
    return 0;
  }

  const std::string& LibraryPath() const { return _libraryPath; }
  Offset LibraryBase() const { return _libraryBase; }
  Offset LibraryLimit() const { return _libraryLimit; }
  Offset ArenaOffset() const { return _arenaOffset; }
  Offset PoolsLimitOffset() const { return _poolsLimitOffset; }
  Offset NumFreePoolsOffset() const { return _numFreePoolsOffset; }
  Offset MaxPoolsOffset() const { return _maxPoolsOffset; }
  Offset AvailablePoolsOffset() const { return _availablePoolsOffset; }
  Offset NextOffset() const { return _nextOffset; }
  Offset PrevOffset() const { return _prevOffset; }
  Offset ArenaStructSize() const { return _arenaStructSize; }
  Offset NumArenas() const { return _numArenas; }
  Offset ArenaStructArray() const { return _arenaStructArray; }
  Offset ArenaStructCount() const { return _arenaStructCount; }
  Offset ArenaStructArrayLimit() const { return _arenaStructArrayLimit; }
  Offset ArenaSize() const { return _arenaSize; }
  Offset PoolSize() const { return _poolSize; }
  Offset MaxPoolsIfAligned() const { return _maxPoolsIfAligned; }
  Offset MaxPoolsIfNotAligned() const { return _maxPoolsIfNotAligned; }
  bool AllArenasAreAligned() const { return _allArenasAreAligned; }
  const char* PYTHON_ARENA;

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  std::string _libraryPath;
  Offset _libraryBase;
  Offset _libraryLimit;
  bool _isResolved;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  const Offset _arenaOffset = 0;
  const Offset _poolsLimitOffset;
  const Offset _numFreePoolsOffset;
  const Offset _maxPoolsOffset;
  const Offset _availablePoolsOffset;
  const Offset _nextOffset;
  const Offset _prevOffset;
  const Offset _arenaStructSize;
  Offset _numArenas;
  Offset _arenaStructArray;
  Offset _arenaStructCount;
  Offset _arenaStructArrayLimit;
  Offset _arenaSize;
  Offset _poolSize;
  Offset _maxPoolsIfAligned;
  Offset _maxPoolsIfNotAligned;
  bool _allArenasAreAligned;
  std::vector<uint32_t> _activeIndices;

  void FindArenaStructArray(
      const typename ModuleDirectory<Offset>::RangeToFlags& rangeToFlags) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (typename ModuleDirectory<Offset>::RangeToFlags::const_iterator
             itRange = rangeToFlags.begin();
         itRange != rangeToFlags.end(); ++itRange) {
      int flags = itRange->_value;
      if ((flags & RangeAttributes::IS_WRITABLE) != 0) {
        Offset base = itRange->_base;
        Offset limit = itRange->_limit;
        for (Offset moduleAddr = base; moduleAddr < limit;
             moduleAddr += sizeof(Offset)) {
          Offset arenaStruct0 = moduleReader.ReadOffset(moduleAddr, 0xbad);
          if ((arenaStruct0 & (sizeof(Offset) - 1)) != 0) {
            continue;
          }
          if (arenaStruct0 == 0) {
            continue;
          }

          Offset arena0 = reader.ReadOffset(arenaStruct0, 0xbad);
          if (arena0 == 0 || (arena0 & (sizeof(Offset) - 1)) != 0) {
            /*
             * The very first arena won't ever be given back, because
             * some of those allocations will be needed pretty much
             * forever.
             */
            continue;
          }
          Offset poolsLimit0 =
              reader.ReadOffset(arenaStruct0 + _poolsLimitOffset, 0xbad);
          if ((poolsLimit0 & 0xfff) != 0 || poolsLimit0 < arena0) {
            continue;
          }

          uint32_t numFreePools0 =
              reader.ReadU32(arenaStruct0 + _numFreePoolsOffset, 0xbad);
          uint32_t maxPools0 =
              reader.ReadU32(arenaStruct0 + _maxPoolsOffset, 0xbad);
          if (maxPools0 == 0 || numFreePools0 > maxPools0) {
            continue;
          }
          Offset numNeverUsedPools0 = numFreePools0;

          Offset firstAvailablePool =
              reader.ReadOffset(arenaStruct0 + _availablePoolsOffset, 0xbad);
          if (firstAvailablePool != 0) {
            Offset availablePool = firstAvailablePool;
            for (; availablePool != 0;
                 availablePool = reader.ReadOffset(
                     availablePool + 2 * sizeof(Offset), 0xbad)) {
              if ((availablePool & 0xfff) != 0) {
                break;
              }
              if (numNeverUsedPools0 == 0) {
                break;
              }
              --numNeverUsedPools0;
            }
            if (availablePool != 0) {
              continue;
            }
          }

          Offset poolSize =
              ((poolsLimit0 - arena0) / (maxPools0 - numNeverUsedPools0)) &
              ~0xfff;

          if (poolSize == 0) {
            continue;
          }

          if ((poolsLimit0 & (poolSize - 1)) != 0) {
            continue;
          }

          Offset arenaSize = maxPools0 * poolSize;
          if ((arena0 & (poolSize - 1)) != 0) {
            arenaSize += poolSize;
          }
          Offset maxPoolsIfAligned = arenaSize / poolSize;
          Offset maxPoolsIfNotAligned = maxPoolsIfAligned - 1;

          Offset arenaStruct = arenaStruct0 + _arenaStructSize;
          bool freeListTrailerFound = false;
          for (;; arenaStruct += _arenaStructSize) {
            Offset arena = reader.ReadOffset(arenaStruct, 0xbad);
            Offset nextArenaStruct =
                reader.ReadOffset(arenaStruct + _nextOffset, 0xbad);
            if (arena == 0) {
              /*
               * The arena is not allocated.  The only live field other
               * than the address is the next pointer, which is
               * constrained to be either null or a pointer to an
               * element in the array.
               */
              if (nextArenaStruct != 0) {
                /*
                 * This pointer is constrained to either be 0 or to put
                 * to somewhere in the array of arena structs.
                 */
                if (nextArenaStruct < arenaStruct0) {
                  break;
                }
                if (((nextArenaStruct - arenaStruct0) % _arenaStructSize) !=
                    0) {
                  break;
                }
              } else {
                if (freeListTrailerFound) {
                  break;
                }
                freeListTrailerFound = true;
              }
            } else {
              /*
               * The arena is allocated.  We can't really evaluate the
               * next unless the prev is also set because the next
               * may be residue from before the arena was allocated.
               */
              uint32_t numFreePools =
                  reader.ReadU32(arenaStruct + _numFreePoolsOffset, 0xbad);
              uint32_t maxPools =
                  reader.ReadU32(arenaStruct + _maxPoolsOffset, 0xbad);
              if (maxPools != (((arena & (poolSize - 1)) == 0)
                                   ? maxPoolsIfAligned
                                   : maxPoolsIfNotAligned)) {
                break;
              }
              if (numFreePools > maxPools) {
                break;
              }
              Offset poolsLimit =
                  reader.ReadOffset(arenaStruct + _poolsLimitOffset, 0xbad);
              if (poolsLimit < arena || poolsLimit > (arena + arenaSize) ||
                  (poolsLimit & (poolSize - 1)) != 0) {
                break;
              }

              /*
               * Note that we don't bother to check the next and prev
               * links for arena structs with allocated arena structs
               * because the links are live only for arenas that still
               * are considered usable for allocations.
               */
            }
          }
          Offset arenaStructArrayLimit = arenaStruct;
          for (arenaStruct -= _arenaStructSize; arenaStruct > arenaStruct0;
               arenaStruct -= _arenaStructSize) {
            if (reader.ReadOffset(arenaStruct, 0xbad) == 0 &&
                reader.ReadOffset(arenaStruct + _nextOffset, 0xbad) >
                    arenaStructArrayLimit) {
              arenaStructArrayLimit = arenaStruct;
            }
          }
          Offset numValidArenaStructs =
              (arenaStructArrayLimit - arenaStruct0) / _arenaStructSize;
          if (_arenaStructCount < numValidArenaStructs) {
            _arenaStructCount = numValidArenaStructs;
            _arenaStructArray = arenaStruct0;
            _arenaStructArrayLimit = arenaStructArrayLimit;
            _poolSize = poolSize;
            _arenaSize = arenaSize;
            _maxPoolsIfAligned = maxPoolsIfAligned;
            _maxPoolsIfNotAligned = maxPoolsIfNotAligned;
          }
        }
      }
    }
    for (Offset arenaStruct = _arenaStructArray;
         arenaStruct < _arenaStructArrayLimit;
         arenaStruct += _arenaStructSize) {
      Offset arena = reader.ReadOffset(arenaStruct + _arenaOffset, 0);
      if (arena != 0) {
        _numArenas++;
        if ((arena & (_poolSize - 1)) != 0) {
          _allArenasAreAligned = false;
        }
      }
    }
    _activeIndices.reserve(_numArenas);
    for (Offset arenaStruct = _arenaStructArray;
         arenaStruct < _arenaStructArrayLimit;
         arenaStruct += _arenaStructSize) {
      Offset arena = reader.ReadOffset(arenaStruct + _arenaOffset, 0);
      if (arena != 0) {
        _activeIndices.push_back((arenaStruct - _arenaStructArray) /
                                 _arenaStructSize);
        if (_allArenasAreAligned &&
            !_virtualMemoryPartition.ClaimRange(arena, _arenaSize, PYTHON_ARENA,
                                                true)) {
          std::cerr << "Warning: Python arena at 0x" << std::hex << arena
                    << " was already marked as something else.\n";
        }
      }
    }
    std::sort(_activeIndices.begin(), _activeIndices.end(), [&](uint32_t i0,
                                                                uint32_t i1) {
      return reader.ReadOffset(this->_arenaStructArray +
                                   ((Offset)(i0)) * this->_arenaStructSize,
                               0xbad) <
             reader.ReadOffset(this->_arenaStructArray +
                                   ((Offset)(i1)) * this->_arenaStructSize,
                               0xbad);
    });
  }
};
}  // namespace Python
}  // namespace chap
