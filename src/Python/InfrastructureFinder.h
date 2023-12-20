// Copyright (c) 2020-2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <algorithm>
#include <regex>
#include <unordered_set>
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"
#include "TypeDirectory.h"

namespace chap {
namespace Python {
template <typename Offset>
class InfrastructureFinder {
 public:
  static constexpr Offset TYPE_IN_PYOBJECT = sizeof(Offset);
  static constexpr Offset LENGTH_IN_STR = 2 * sizeof(Offset);
  static constexpr Offset UNKNOWN_OFFSET = ~0;
  enum MajorVersion { Version2, Version3, VersionUnknownOrOther };
  InfrastructureFinder(const ModuleDirectory<Offset>& moduleDirectory,
                       VirtualMemoryPartition<Offset>& partition,
                       TypeDirectory<Offset>& typeDirectory)
      : PYTHON_ARENA("python arena"),
        _moduleDirectory(moduleDirectory),
        _majorVersion(VersionUnknownOrOther),
        _isResolved(false),
        _virtualMemoryPartition(partition),
        _virtualAddressMap(partition.GetAddressMap()),
        _typeDirectory(typeDirectory),
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
        _allArenasAreAligned(true),
        _typeType(0),
        _typeSize(0),
        _baseInType(UNKNOWN_OFFSET),
        _objectType(0),
        _dictInType(UNKNOWN_OFFSET),
        _getSetInType(UNKNOWN_OFFSET),
        _dictType(0),
        _keysInDict(UNKNOWN_OFFSET),
        _valuesInDict(UNKNOWN_OFFSET),
        _dictKeysHeaderSize(UNKNOWN_OFFSET),
        _sizeInDictKeys(UNKNOWN_OFFSET),
        _numElementsInDictKeys(UNKNOWN_OFFSET),
        _dictKeysHaveIndex(false),
        _logarithmicSizeInKeys(false),
        _strType(0),
        _cstringInStr(UNKNOWN_OFFSET),
        _listType(0),
        _sizeInList(2 * sizeof(Offset)),
        _itemsInList(3 * sizeof(Offset)),
        _tupleType(0),
        _intType(0),
        _bytesType(0),
        _floatType(0),
        _dequeType(0),
        _firstBlockInDeque(2 * sizeof(Offset)),
        _lastBlockInDeque(3 * sizeof(Offset)),
        _forwardInDequeBlock(62 * sizeof(Offset)),
        _mainInterpreterState(0),
        _builtinsInInterpreterState(UNKNOWN_OFFSET),
        _garbageCollectionHeaderSize(UNKNOWN_OFFSET),
        _garbageCollectionRefcntShift(0),
        _refcntInGarbageCollectionHeader(2 * sizeof(Offset)),
        _cachedKeysInHeapTypeObject(UNKNOWN_OFFSET) {}
  void Resolve() {
    if (_isResolved) {
      abort();
    }
    if (!_moduleDirectory.IsResolved()) {
      abort();
    }

    std::regex moduleRegex("^.*/(lib)?python([23])?[^/]+$");
    std::smatch moduleSmatch;
    const typename ModuleDirectory<Offset>::ModuleInfo* exeModuleInfo = nullptr;
    const typename ModuleDirectory<Offset>::ModuleInfo* libModuleInfo = nullptr;
    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      const std::string& modulePath = nameAndModuleInfo.first;
      if (!std::regex_match(modulePath, moduleSmatch, moduleRegex)) {
        continue;
      }

      std::string majorVersion = moduleSmatch[2];
      if (!majorVersion.empty()) {
        if (majorVersion == "2") {
          if (_majorVersion != VersionUnknownOrOther &&
              !(_majorVersion == Version2)) {
            std::cerr << "Warning: error finding major python version.\n";
          }
          _majorVersion = Version2;
        } else if (majorVersion == "3") {
          if (_majorVersion != VersionUnknownOrOther &&
              !(_majorVersion == Version3)) {
            std::cerr << "Warning: error finding major python version.\n";
          }
          _majorVersion = Version3;
        }
      }
      if (moduleSmatch[1].length() > 0) {
        if (!_libraryPath.empty()) {
          std::cerr << "Warning: error finding python library path.\n";
        }
        libModuleInfo = &(nameAndModuleInfo.second);
        _libraryPath = modulePath;
      } else {
        if (!_executablePath.empty()) {
          std::cerr << "Warning: error finding python executable path.\n";
        }
        exeModuleInfo = &(nameAndModuleInfo.second);
        _executablePath = modulePath;
      }
    }

    if (libModuleInfo != nullptr) {
      FindArenaStructArrayAndTypes(*libModuleInfo);
    }
    if (exeModuleInfo != nullptr) {
      if (_arenaStructArray == 0) {
        FindArenaStructArrayAndTypes(*exeModuleInfo);
      }
    }
    _garbageCollectionRefcntShift =
        (_majorVersion == Version2)
            ? 0
            : (_majorVersion == Version3)
                  ? 1
                  : (_keysInDict == PYTHON2_KEYS_IN_DICT) ? 0 : 1;
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
  const std::vector<uint32_t>& ActiveIndices() const { return _activeIndices; }
  Offset PoolSize() const { return _poolSize; }
  Offset MaxPoolsIfAligned() const { return _maxPoolsIfAligned; }
  Offset MaxPoolsIfNotAligned() const { return _maxPoolsIfNotAligned; }
  bool AllArenasAreAligned() const { return _allArenasAreAligned; }
  Offset TypeType() const { return _typeType; }
  Offset TypeSize() const { return _typeSize; }
  Offset BaseInType() const { return _baseInType; }
  Offset ObjectType() const { return _objectType; }
  Offset DictInType() const { return _dictInType; }
  Offset DictType() const { return _dictType; }
  Offset KeysInDict() const { return _keysInDict; }
  Offset ValuesInDict() const { return _valuesInDict; }
  Offset DictKeysHeaderSize() const { return _dictKeysHeaderSize; }
  Offset StrType() const { return _strType; }
  Offset CstringInStr() const { return _cstringInStr; }
  Offset ListType() const { return _listType; }
  Offset SizeInList() const { return _sizeInList; }
  Offset ItemsInList() const { return _itemsInList; }
  Offset TupleType() const { return _tupleType; }
  Offset IntType() const { return _intType; }
  Offset BytesType() const { return _bytesType; }
  Offset FloatType() const { return _floatType; }
  Offset DequeType() const { return _dequeType; }
  Offset FirstBlockInDeque() const { return _firstBlockInDeque; }
  Offset LastBlockInDeque() const { return _lastBlockInDeque; }
  Offset ForwardInDequeBlock() const { return _forwardInDequeBlock; }
  const std::vector<Offset> NonEmptyGarbageCollectionLists() const {
    return _nonEmptyGarbageCollectionLists;
  }
  const Offset GarbageCollectionHeaderSize() const {
    return _garbageCollectionHeaderSize;
  }
  const Offset GarbageCollectionRefcntShift() const {
    return _garbageCollectionRefcntShift;
  }
  const Offset RefcntInGarbageCollectionHeader() const {
    return _refcntInGarbageCollectionHeader;
  }
  const Offset CachedKeysInHeapTypeObject() const {
    return _cachedKeysInHeapTypeObject;
  }
  const char* PYTHON_ARENA;

  const std::string& GetTypeName(Offset typeObject) const {
    return _typeDirectory.GetTypeName(typeObject);
  }

  bool HasType(Offset typeObject) const {
    return _typeDirectory.HasType(typeObject);
  }

  bool IsATypeType(Offset typeObject) const {
    int depth = 0;
    Reader reader(_virtualAddressMap);
    while (typeObject != 0) {
      if (typeObject == _typeType) {
        return true;
      }
      if ((reader.ReadOffset(typeObject + TYPE_IN_PYOBJECT, 0) &
           (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      if (++depth == 100) {
        /*
         * This can happen occasionally, but generally just because
         * the original typeObject has a pointer to something similar
         * at offset _baseInType.  We don't bother warning because
         * this simply means that the typeObject argument passed in
         * didn't refer to a type type.
         */
        return false;
      }
      typeObject = reader.ReadOffset(typeObject + _baseInType, 0);
    }
    return false;
  }

  void GetTriplesAndLimitFromDict(Offset dict, Offset& triples,
                                  Offset& limit) const {
    triples = 0;
    limit = 0;
    Reader reader(_virtualAddressMap);
    Offset keys = reader.ReadOffset(dict + _keysInDict, 0xbad);

    if ((keys & (sizeof(Offset) - 1)) != 0) {
      return;
    }

    if (_dictKeysHeaderSize > 0) {
      GetTriplesAndLimitFromDictKeys(keys, triples, limit);
    } else {
      Offset entrySize = (Offset)(3) * (Offset)(sizeof(Offset));
      Offset capacity = reader.ReadOffset(dict + PYTHON2_MASK_IN_DICT, ~0) + 1;
      triples = keys;
      limit = triples + capacity * entrySize;
    }
  }

  void GetTriplesAndLimitFromDictKeys(Offset keys, Offset& triples,
                                      Offset& limit) const {
    triples = 0;
    limit = 0;
    if (_dictKeysHeaderSize == 0) {
      return;
    }
    Reader reader(_virtualAddressMap);

    if ((keys & (sizeof(Offset) - 1)) != 0) {
      return;
    }

    Offset entrySize = (Offset)(3) * (Offset)(sizeof(Offset));
    Offset capacity = reader.ReadOffset(keys + _sizeInDictKeys, 0);
    if (_logarithmicSizeInKeys) {
      capacity = 1 << (capacity & 0xff);
    } else {
      if ((capacity & (capacity - 1)) != 0) {
        return;
      }
    }
    triples = keys + _dictKeysHeaderSize;
    if (_dictKeysHaveIndex) {
      triples += (capacity * ((capacity < 0x80) ? ((Offset)(1))
                                                : (capacity < 0x8000)
                                                      ? ((Offset)(2))
                                                      : (capacity < 0x80000000)
                                                            ? ((Offset)(4))
                                                            : ((Offset)(8))));
      Offset numElements = reader.ReadOffset(keys + _numElementsInDictKeys, 0);
      limit = triples + numElements * entrySize;
    } else {
      limit = triples + capacity * entrySize;
    }
  }

  void ClaimArenaRangesIfNeeded() {
    if (_allArenasAreAligned) {
      return;
    }
    Reader reader(_virtualAddressMap);
    for (Offset arenaStruct = _arenaStructArray;
         arenaStruct < _arenaStructArrayLimit;
         arenaStruct += _arenaStructSize) {
      Offset arena = reader.ReadOffset(arenaStruct + _arenaOffset, 0);
      if (arena == 0) {
        continue;
      }

      if (_virtualMemoryPartition.IsClaimed(arena)) {
        continue;
      }
      /*
       * Attempt to claim the arena.  We do not treat it as an anchor area
       * because it is a source of allocations.
       */
      if (!_virtualMemoryPartition.ClaimRange(arena, _arenaSize, PYTHON_ARENA,
                                              false)) {
        std::cerr << "Warning: Part of the python arena at 0x" << std::hex
                  << arena << " was already marked as something else.\n";
      }
    }
  }

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  MajorVersion _majorVersion;
  std::string _libraryPath;
  std::string _executablePath;
  bool _isResolved;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  TypeDirectory<Offset>& _typeDirectory;
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
  /*
   * These constants are private because we want any users of this
   * infrastructure to used the calculated values rather than these
   * constants.
   */
  static constexpr Offset PYTHON2_MASK_IN_DICT = 4 * sizeof(Offset);
  static constexpr Offset PYTHON2_KEYS_IN_DICT = 5 * sizeof(Offset);
  static constexpr Offset PYTHON2_DICT_KEYS_HEADER_SIZE = 0;
  static constexpr Offset PYTHON2_CSTRING_IN_STR = 0x24;
  static constexpr Offset PYTHON3_5_KEYS_IN_DICT = 3 * sizeof(Offset);
  static constexpr Offset PYTHON3_6_KEYS_IN_DICT = 4 * sizeof(Offset);
  static constexpr Offset PYTHON3_11_KEYS_IN_DICT = 4 * sizeof(Offset);
  static constexpr Offset PYTHON3_SIZE_IN_DICT_KEYS = sizeof(Offset);
  static constexpr Offset PYTHON3_5_DICT_KEYS_HEADER_SIZE = 4 * sizeof(Offset);
  static constexpr Offset PYTHON3_6_NUM_ELEMENTS_IN_DICT_KEYS =
      4 * sizeof(Offset);
  static constexpr Offset PYTHON3_6_DICT_KEYS_HEADER_SIZE = 5 * sizeof(Offset);
  static constexpr Offset PYTHON3_11_NUM_ELEMENTS_IN_DICT_KEYS =
      3 * sizeof(Offset);
  static constexpr Offset PYTHON3_11_DICT_KEYS_HEADER_SIZE = 4 * sizeof(Offset);
  static constexpr Offset PYTHON3_CSTRING_IN_STR = 6 * sizeof(Offset);
  Offset _typeType;
  Offset _typeSize;
  Offset _baseInType;
  Offset _objectType;
  Offset _dictInType;
  Offset _getSetInType;
  Offset _dictType;
  Offset _keysInDict;
  Offset _valuesInDict;
  Offset _dictKeysHeaderSize;
  Offset _sizeInDictKeys;
  Offset _numElementsInDictKeys;
  bool _dictKeysHaveIndex;
  bool _logarithmicSizeInKeys;
  Offset _strType;
  Offset _cstringInStr;
  Offset _listType;
  Offset _sizeInList;
  Offset _itemsInList;
  Offset _tupleType;
  Offset _intType;
  Offset _bytesType;
  Offset _floatType;
  Offset _dequeType;
  Offset _firstBlockInDeque;
  Offset _lastBlockInDeque;
  Offset _forwardInDequeBlock;
  Offset _mainInterpreterState;
  Offset _builtinsInInterpreterState;
  std::vector<uint32_t> _activeIndices;
  std::vector<Offset> _nonEmptyGarbageCollectionLists;
  Offset _garbageCollectionHeaderSize;
  Offset _garbageCollectionRefcntShift;
  Offset _refcntInGarbageCollectionHeader;
  Offset _cachedKeysInHeapTypeObject;

  void FindMajorVersionFromPaths() {}
  void FindArenaStructArrayAndTypes(
      const typename ModuleDirectory<Offset>::ModuleInfo& moduleInfo) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    Offset bestBase = 0;
    Offset bestLimit = 0;
    const auto& ranges = moduleInfo._ranges;
    if (ranges.empty()) {
      return;
    }
    Offset moduleBase = ranges.begin()->_base;
    Offset moduleLimit = ranges.rbegin()->_limit;
    for (const auto& range : ranges) {
      int flags = range._value._flags;
      if ((flags & RangeAttributes::IS_WRITABLE) != 0) {
        Offset base = range._base;
        Offset limit = range._limit;

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
            bestBase = base;
            bestLimit = limit;
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
        /*
         * Attempt to claim the arena.  We do not treat it as an anchor area
         * because it is a source of allocations.
         */
        if (_allArenasAreAligned &&
            !_virtualMemoryPartition.ClaimRange(arena, _arenaSize, PYTHON_ARENA,
                                                false)) {
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
    if (_arenaStructCount != 0) {
      FindTypes(moduleBase, moduleLimit, bestBase, bestLimit, reader);
      if (_typeType != 0) {
        FindNonEmptyGarbageCollectionLists(bestBase, bestLimit, reader);
        FindDynamicallyAllocatedTypes();
      }
    }
  }

  /*
   * This is not as expensive as it looks, as it normally converges within the
   * first 10 blocks in the first pool of the first arena.
   */
  void FindTypes(Offset moduleBase, Offset moduleLimit, Offset base,
                 Offset limit, Reader& reader) {
    if (_majorVersion == VersionUnknownOrOther) {
      /*
       * At present this could happen in the case of a statically linked
       * python where chap also is not able to derive the correct name of
       * the main executable or in the very unusual case that an older
       * version was being used.  Derivation of the main executable name
       * works for cores generated by reasonably recent versions of gdb
       * where the module paths are in the PT_NOTE section, but some
       * improvement could be made for the older case.  At some point
       * python4 will exist.
       */
      std::cerr << "Warning: the major version of python was not derived "
                   "successfully from module paths.\n";
      std::cerr << "An attempt will be made to derive needed offsets.\n";
    }
    for (Offset arenaStruct = _arenaStructArray;
         arenaStruct < _arenaStructArrayLimit;
         arenaStruct += _arenaStructSize) {
      Offset arena = reader.ReadOffset(arenaStruct + _arenaOffset, 0);
      if (arena == 0) {
        continue;
      }
      Offset firstPool = (arena + (_poolSize - 1)) & ~(_poolSize - 1);
      Offset poolsLimit = (arena + _arenaSize) & ~(_poolSize - 1);
      for (Offset pool = firstPool; pool < poolsLimit; pool += _poolSize) {
        if (reader.ReadU32(pool, 0) == 0) {
          continue;
        }
        Offset blockSize = _poolSize - reader.ReadU32(pool + 0x2c, 0);
        if (blockSize == 0) {
          continue;
        }
        Offset poolLimit = pool + _poolSize;
        for (Offset block = pool + 0x30; block + blockSize <= poolLimit;
             block += blockSize) {
          Offset candidateType =
              reader.ReadOffset(block + TYPE_IN_PYOBJECT, 0xbadbad);
          if ((candidateType & (sizeof(Offset) - 1)) != 0) {
            continue;
          }
          Offset candidateTypeType =
              reader.ReadOffset(candidateType + 8, 0xbadbad);
          if ((candidateTypeType & (sizeof(Offset) - 1)) != 0) {
            continue;
          }
          if (candidateTypeType !=
              reader.ReadOffset(candidateTypeType + TYPE_IN_PYOBJECT,
                                0xbadbad)) {
            continue;
          }
          if (candidateTypeType < moduleBase ||
              candidateTypeType >= moduleLimit) {
            continue;
          }
          Offset typeSize =
              reader.ReadOffset(candidateTypeType + 4 * sizeof(Offset), ~0);
          if (typeSize >= 0x800) {
            continue;
          }
          Offset baseInType = 0x18 * sizeof(Offset);
          for (; baseInType < typeSize - 0x10; baseInType += sizeof(Offset)) {
            Offset candidateObjType =
                reader.ReadOffset(candidateTypeType + baseInType, 0xbad);
            if ((candidateObjType & (sizeof(Offset) - 1)) != 0) {
              continue;
            }
            Offset candidateDict = reader.ReadOffset(
                candidateTypeType + baseInType + sizeof(Offset), 0xbad);
            if ((candidateDict & (sizeof(Offset) - 1)) != 0) {
              continue;
            }
            if (reader.ReadOffset(candidateObjType + TYPE_IN_PYOBJECT, 0) !=
                candidateTypeType) {
              continue;
            }

            if (reader.ReadOffset(candidateObjType + baseInType, 0xbad) != 0) {
              continue;
            }
            Offset candidateDictType =
                reader.ReadOffset(candidateDict + TYPE_IN_PYOBJECT, 0);
            if (reader.ReadOffset(candidateDictType + TYPE_IN_PYOBJECT,
                                  0xbad) != candidateTypeType) {
              continue;
            }
            if (reader.ReadOffset(candidateDictType + baseInType, 0xbad) !=
                candidateObjType) {
              continue;
            }
            _typeType = candidateTypeType;
            _typeSize = typeSize;
            _baseInType = baseInType;
            _objectType = candidateObjType;
            _dictInType = baseInType + sizeof(Offset);
            _getSetInType = baseInType - sizeof(Offset);
            _dictType = candidateDictType;
            _typeDirectory.RegisterType(_typeType, "type");
            _typeDirectory.RegisterType(_objectType, "object");
            _typeDirectory.RegisterType(_dictType, "dict");

            /*
             * The dict for the type type is non-empty and contains multiple
             * string keys.  This allows deriving or checking offsets
             * associated with dict and with str.
             */
            if (!CalculateOffsetsForDictAndStr(candidateDict)) {
              return;
            }

            FindStaticallyAllocatedTypes(reader);

            FindMainInterpreterStateAndBuiltinNames(base, limit);
            return;
          }
        }
      }
    }
  }

  bool SetHtCachedKeysOffset(Reader& reader, Offset typeCandidate) {
    for (Offset keysOffset = _typeSize - 0x10 * sizeof(Offset);
         keysOffset < _typeSize; keysOffset += sizeof(Offset)) {
      Offset keysCandidate =
          reader.ReadOffset(typeCandidate + keysOffset, 0xbad);
      if ((keysCandidate & (sizeof(Offset) - 1)) != 0) {
        continue;
      }
      if (reader.ReadOffset(keysCandidate, 0) != 1) {
        /*
         * This is not true of PyDictKeysObject in general, because the
         * ref count can quite easily be something other than 1, but it
         * happens to be true for most of the ones that are referenced
         * from type objects and in fact we need just one to figure
         * out the offset.
         */
        continue;
      }
      Offset size = reader.ReadOffset(keysCandidate + sizeof(Offset), 0);
      if (size == 0 || ((size | (size - 1)) != (size ^ (size - 1)))) {
        continue;
      }
      Offset usable =
          reader.ReadOffset(keysCandidate + 3 * sizeof(Offset), 0xbad);
      if (size - 1 != usable) {
        continue;
      }
      if (usable < reader.ReadOffset(keysCandidate + 4 * sizeof(Offset), ~0)) {
        continue;
      }
      _cachedKeysInHeapTypeObject = keysOffset;
      return true;
    }
    return false;
  }

  void FindDynamicallyAllocatedTypes() {
    bool needHtCachedKeysOffset = (_majorVersion != Version2);
    Reader reader(_virtualAddressMap);
    Reader otherReader(_virtualAddressMap);
    std::unordered_set<Offset> deferredTypeChecks;
    for (auto listHead : _nonEmptyGarbageCollectionLists) {
      Offset prevNode = listHead;
      for (Offset node =
               reader.ReadOffset(listHead, listHead) & ~(sizeof(Offset) - 1);
           node != listHead;
           node = reader.ReadOffset(node, listHead) & ~(sizeof(Offset) - 1)) {
        if ((reader.ReadOffset(node + sizeof(Offset), 0) &
             ~(sizeof(Offset) - 1)) != prevNode) {
          std::cerr << "Warning: GC list at 0x" << std::hex << listHead
                    << " is ill-formed near 0x" << node << ".\n";
          break;
        }
        prevNode = node;
        Offset typeCandidate = node + _garbageCollectionHeaderSize;
        if (_typeDirectory.HasType(typeCandidate)) {
          continue;
        }
        Offset typeTypeCandidate =
            reader.ReadOffset(typeCandidate + TYPE_IN_PYOBJECT, 0);
        if (typeTypeCandidate == 0) {
          continue;
        }
        if (IsATypeType(typeTypeCandidate)) {
          _typeDirectory.RegisterType(typeCandidate, "");
          if (needHtCachedKeysOffset &&
              SetHtCachedKeysOffset(otherReader, typeCandidate)) {
            needHtCachedKeysOffset = false;
          }
        } else {
          deferredTypeChecks.insert(typeTypeCandidate);
        }
      }
    }
    /*
     * This is needed because type objects may be statically allocated in
     * plugins.  An alternative would be to scan those modules for type
     * declarations.
     */

    for (Offset typeCandidate : deferredTypeChecks) {
      if (_typeDirectory.HasType(typeCandidate)) {
        continue;
      }
      Offset typeTypeCandidate =
          reader.ReadOffset(typeCandidate + TYPE_IN_PYOBJECT, 0);
      if (typeTypeCandidate != 0 && IsATypeType(typeTypeCandidate)) {
        _typeDirectory.RegisterType(typeCandidate, "");
      }
    }
  }

  void FindStaticallyAllocatedTypes(Reader& reader) {
    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      for (const auto& range : nameAndModuleInfo.second._ranges) {
        if ((range._value._flags & RangeAttributes::IS_WRITABLE) != 0) {
          FindStaticallyAllocatedTypes(range._base, range._limit, reader);
        }
      }
    }
  }

  void CheckForSpecialBuiltins(Offset pythonType,
                               const std::string& currentName) {
    if (_listType == 0 && currentName == "list") {
      _listType = pythonType;
      return;
    }
    if (_tupleType == 0 && currentName == "tuple") {
      _tupleType = pythonType;
      return;
    }
    if (_intType == 0 && currentName == "int") {
      _intType = pythonType;
      return;
    }
    if (_bytesType == 0 && currentName == "bytes") {
      _bytesType = pythonType;
      return;
    }
    if (_floatType == 0 && currentName == "float") {
      _floatType = pythonType;
      return;
    }
    if (_dequeType == 0 && currentName == "collections.deque") {
      _dequeType = pythonType;
    }
  }

  void FindStaticallyAllocatedTypes(Offset base, Offset limit, Reader& reader) {
    Offset candidateLimit = limit - _typeSize + 1;
    Offset candidate = base;
    Reader baseTypeReader(_virtualAddressMap);
    while (candidate < candidateLimit) {
      if (!_typeDirectory.HasType(candidate) &&
          reader.ReadOffset(candidate + TYPE_IN_PYOBJECT, 0xbad) == _typeType) {
        Offset baseType = reader.ReadOffset(candidate + _baseInType, 0);
        if (baseType != 0) {
          if (baseType == _objectType ||
              (_typeDirectory.HasType(baseType) ||
               baseTypeReader.ReadOffset(baseType + TYPE_IN_PYOBJECT, 0) ==
                   _typeType)) {
            CheckForSpecialBuiltins(candidate,
                                    _typeDirectory.RegisterType(candidate, ""));
            candidate += _baseInType;
            continue;
          }
        } else if (candidate != _objectType) {
          /*
           * For python 3, at least type "object" has no base type, but that
           * is OK because at this point we have already located the
           * corresponding type object.  For Python 2, there are other types
           * that do not inherit from anything, including at least cell,
           * methoddescriptor and classmethoddescriptor.
           */
          Offset getSet = reader.ReadOffset(candidate + _getSetInType, 0);
          if (getSet >= base && getSet < limit) {
            CheckForSpecialBuiltins(candidate,
                                    _typeDirectory.RegisterType(candidate, ""));
          }
        }
      }
      candidate += sizeof(Offset);
    }
  }

  /*
   * The following function attempts to use the specified dict
   * to determine names for any built-in types for which the name was
   * statically allocated and didn't make it into the core.  This can
   * happen because it is not uncommon for gdb to not keep images for
   * things that can be obtained from the main executable or from
   * shared libraries.
   */
  void RegisterBuiltinTypesFromDict(Reader& reader, Offset dict) {
    Offset triples = 0;
    Offset triplesLimit = 0;
    GetTriplesAndLimitFromDict(dict, triples, triplesLimit);
    for (Offset triple = triples; triple < triplesLimit;
         triple += (3 * sizeof(Offset))) {
      Offset key = reader.ReadOffset(triple + sizeof(Offset), 0);
      if (key == 0) {
        continue;
      }
      Offset value = reader.ReadOffset(triple + 2 * sizeof(Offset), 0);
      if (value == 0) {
        continue;
      }
      const char* image;
      Offset numBytesFound =
          _virtualAddressMap.FindMappedMemoryImage(key, &image);
      if (numBytesFound < _cstringInStr + 2) {
        continue;
      }
      if (*((Offset*)(image + TYPE_IN_PYOBJECT)) != _strType) {
        continue;
      }
      Offset length = *((Offset*)(image + LENGTH_IN_STR));
      if (numBytesFound < _cstringInStr + length + 1) {
        continue;
      }
      if (reader.ReadOffset(value + TYPE_IN_PYOBJECT, 0) != _typeType) {
        continue;
      }

      CheckForSpecialBuiltins(
          value, _typeDirectory.RegisterType(value, image + _cstringInStr));
    }
  }

  int CountBuiltinTypesFromDict(Reader& reader, Offset dict) {
    int typeCount = 0;
    Offset triples = 0;
    Offset triplesLimit = 0;
    GetTriplesAndLimitFromDict(dict, triples, triplesLimit);
    for (Offset triple = triples; triple < triplesLimit;
         triple += (3 * sizeof(Offset))) {
      Offset key = reader.ReadOffset(triple + sizeof(Offset), 0);
      if (key == 0) {
        continue;
      }
      Offset value = reader.ReadOffset(triple + 2 * sizeof(Offset), 0);
      if (value == 0) {
        continue;
      }
      const char* image;
      Offset numBytesFound =
          _virtualAddressMap.FindMappedMemoryImage(key, &image);
      if (numBytesFound < _cstringInStr + 2) {
        continue;
      }
      if (*((Offset*)(image + TYPE_IN_PYOBJECT)) != _strType) {
        continue;
      }
      Offset length = *((Offset*)(image + LENGTH_IN_STR));
      if (numBytesFound < _cstringInStr + length + 1) {
        continue;
      }
      if (reader.ReadOffset(value + TYPE_IN_PYOBJECT, 0) != _typeType) {
        continue;
      }

      if (!strcmp(image + _cstringInStr, "type") ||
          !strcmp(image + _cstringInStr, "dict") ||
          !strcmp(image + _cstringInStr, "str") ||
          !strcmp(image + _cstringInStr, "list") ||
          !strcmp(image + _cstringInStr, "tuple") ||
          !strcmp(image + _cstringInStr, "int") ||
          !strcmp(image + _cstringInStr, "float")) {
        typeCount++;
      }
    }
    return typeCount;
  }

  void RegisterImportedTypes(Reader& reader, Offset dictForModule,
                             const char* moduleName) {
    Offset triples = 0;
    Offset triplesLimit = 0;
    GetTriplesAndLimitFromDict(dictForModule, triples, triplesLimit);
    std::string modulePrefix(moduleName);
    modulePrefix.append(".");

    for (Offset triple = triples; triple < triplesLimit;
         triple += (3 * sizeof(Offset))) {
      Offset key = reader.ReadOffset(triple + sizeof(Offset), 0);
      if (key == 0) {
        continue;
      }
      Offset value = reader.ReadOffset(triple + 2 * sizeof(Offset), 0);
      if (value == 0) {
        continue;
      }
      const char* image;
      Offset numBytesFound =
          _virtualAddressMap.FindMappedMemoryImage(key, &image);
      if (numBytesFound < _cstringInStr + 2) {
        continue;
      }
      if (*((Offset*)(image + TYPE_IN_PYOBJECT)) != _strType) {
        continue;
      }
      Offset length = *((Offset*)(image + LENGTH_IN_STR));
      if (numBytesFound < _cstringInStr + length + 1) {
        continue;
      }
      if (reader.ReadOffset(value + TYPE_IN_PYOBJECT, 0) != _typeType) {
        continue;
      }
      std::string unqualifiedName(image + _cstringInStr);
      std::string qualifiedName = modulePrefix + unqualifiedName;
      _typeDirectory.RegisterType(value, qualifiedName);
    }
  }

  void FindMainInterpreterStateAndBuiltinNames(Offset base, Offset limit) {
    Reader reader(_virtualAddressMap);
    Reader iscReader(_virtualAddressMap);
    Reader otherReader(_virtualAddressMap);
    for (Offset mainInterpreterStateRefCandidate = base;
         mainInterpreterStateRefCandidate < limit;
         mainInterpreterStateRefCandidate += sizeof(Offset)) {
      Offset mainInterpreterStateCandidate =
          reader.ReadOffset(mainInterpreterStateRefCandidate, 0xbad);
      if ((mainInterpreterStateCandidate & (sizeof(Offset) - 1)) != 0) {
        continue;
      }
      if (iscReader.ReadOffset(mainInterpreterStateCandidate, 0xbad) != 0) {
        continue;
      }
      Offset threadStateCandidate = iscReader.ReadOffset(
          mainInterpreterStateCandidate + sizeof(Offset), 0xbad);
      if ((threadStateCandidate & (sizeof(Offset) - 1)) != 0) {
        continue;
      }
      if (otherReader.ReadOffset(threadStateCandidate + sizeof(Offset),
                                 0xbad) != mainInterpreterStateCandidate &&
          otherReader.ReadOffset(threadStateCandidate + 2 * sizeof(Offset),
                                 0xbad) != mainInterpreterStateCandidate) {
        continue;
      }

      /*
       * At present, the first dict found in a PyInterpreterState maps
       * from module name to module object.
       */
      Offset firstDict = 0;
      for (Offset o = 2 * sizeof(Offset); o < 16 * sizeof(Offset);
           o += sizeof(Offset)) {
        Offset dictCandidate =
            iscReader.ReadOffset(mainInterpreterStateCandidate + o, 0xbad);
        if ((dictCandidate & (sizeof(Offset) - 1)) != 0) {
          continue;
        }
        if (otherReader.ReadOffset(dictCandidate + TYPE_IN_PYOBJECT, 0xbad) ==
            _dictType) {
          firstDict = dictCandidate;
          break;
        }
      }

      Offset triples = 0;
      Offset triplesLimit = 0;
      GetTriplesAndLimitFromDict(firstDict, triples, triplesLimit);
      if (triplesLimit - triples > 0x3000) {
        // We don't expect that many modules
        continue;
      }

      Offset builtinsModule = 0;
      int bestTypeCount = 0;
      Offset dictForBuiltinsModule = 0;
      Offset moduleType = 0;
      for (Offset triple = triples; triple < triplesLimit;
           triple += (3 * sizeof(Offset))) {
        Offset key = otherReader.ReadOffset(triple + sizeof(Offset), 0);
        if (key == 0) {
          continue;
        }
        const char* keyImage;
        Offset numBytesFound =
            _virtualAddressMap.FindMappedMemoryImage(key, &keyImage);
        if (numBytesFound < _cstringInStr + 2) {
          continue;
        }
        if (*((Offset*)(keyImage + TYPE_IN_PYOBJECT)) != _strType) {
          continue;
        }
        Offset length = *((Offset*)(keyImage + LENGTH_IN_STR));
        if (numBytesFound < _cstringInStr + length + 1) {
          continue;
        }
        if (!strcmp("__builtin__", keyImage + _cstringInStr) ||
            !strcmp("builtins", keyImage + _cstringInStr)) {
          Offset value = otherReader.ReadOffset(triple + 2 * sizeof(Offset), 0);
          if (value == 0) {
            std::cerr << "Error: unable to find module for name"
                      << (keyImage + _cstringInStr) << "\n";
            continue;
          } else {
            moduleType =
                otherReader.ReadOffset(value + TYPE_IN_PYOBJECT, 0xbad);
            _typeDirectory.RegisterType(moduleType, "module");
            Offset dictForModule = otherReader.ReadOffset(
                value + TYPE_IN_PYOBJECT + sizeof(Offset), 0xbad);
            if (otherReader.ReadOffset(dictForModule + TYPE_IN_PYOBJECT, 0) !=
                _dictType) {
              std::cerr
                  << "Error: Unexpected type for dict for builtins module at 0x"
                  << std::hex << value << "\n";
              continue;
            }
            int typeCount =
                CountBuiltinTypesFromDict(otherReader, dictForModule);
            if (typeCount > bestTypeCount) {
              bestTypeCount = typeCount;
              builtinsModule = value;
              dictForBuiltinsModule = dictForModule;
            }
          }
        }
      }
      if (builtinsModule == 0) {
        // We probably didn't actually find a real PyInterpreterState.
        continue;
      }
      RegisterBuiltinTypesFromDict(otherReader, dictForBuiltinsModule);
      _mainInterpreterState = mainInterpreterStateCandidate;
      for (Offset triple = triples; triple < triplesLimit;
           triple += (3 * sizeof(Offset))) {
        Offset module = otherReader.ReadOffset(triple + 2 * sizeof(Offset), 0);
        if (module == builtinsModule) {
          continue;
        }

        Offset moduleName = otherReader.ReadOffset(triple + sizeof(Offset), 0);
        if (moduleName == 0) {
          continue;
        }
        const char* moduleNameImage;
        Offset numBytesFound = _virtualAddressMap.FindMappedMemoryImage(
            moduleName, &moduleNameImage);
        if (numBytesFound < _cstringInStr + 2) {
          continue;
        }
        if (*((Offset*)(moduleNameImage + TYPE_IN_PYOBJECT)) != _strType) {
          std::cerr
              << "Warning: Unexpected key type found in dict of modules\n";
          continue;
        }
        Offset length = *((Offset*)(moduleNameImage + LENGTH_IN_STR));
        if (numBytesFound < _cstringInStr + length + 1) {
          continue;
        }
        Offset expectModuleType =
            otherReader.ReadOffset(module + TYPE_IN_PYOBJECT, 0);
        if (expectModuleType != moduleType) {
          // This can happen, for example, if there is no module of the given
          // name,
          // in which case the value will be set to None.
          continue;
        }

        Offset dictForModule = otherReader.ReadOffset(
            module + TYPE_IN_PYOBJECT + sizeof(Offset), 0);
        Offset expectDictType =
            otherReader.ReadOffset(dictForModule + TYPE_IN_PYOBJECT, 0);
        if (expectDictType != _dictType) {
          std::cerr << "Warning: dict 0x" << std::hex << dictForModule
                    << " for module 0x" << module << " has unexpected type 0x"
                    << expectDictType << "\n";
        }
        RegisterImportedTypes(otherReader, dictForModule,
                              moduleNameImage + _cstringInStr);
      }
      break;
    }
  }

  bool CalculateOffsetsForDictAndStr(Offset dictForTypeType) {
    if (_majorVersion == Version2 || _majorVersion == VersionUnknownOrOther) {
      _keysInDict = PYTHON2_KEYS_IN_DICT;
      _dictKeysHeaderSize = 0;
      _cstringInStr = PYTHON2_CSTRING_IN_STR;
      if (CheckDictAndStrOffsets(dictForTypeType)) {
        return true;
      } else {
        if (_majorVersion == Version2) {
          std::cerr << "Warning: Failed to confirm dict and str offsets for "
                       "python2.\n";
          return false;
        }
      }
    }
    _keysInDict = PYTHON3_5_KEYS_IN_DICT;
    _valuesInDict = _keysInDict + sizeof(Offset);
    _dictKeysHeaderSize = PYTHON3_5_DICT_KEYS_HEADER_SIZE;
    _sizeInDictKeys = PYTHON3_SIZE_IN_DICT_KEYS;
    _cstringInStr = PYTHON3_CSTRING_IN_STR;
    if (CheckDictAndStrOffsets(dictForTypeType)) {
      return true;
    }

    _keysInDict = PYTHON3_6_KEYS_IN_DICT;
    _valuesInDict = _keysInDict + sizeof(Offset);
    _dictKeysHeaderSize = PYTHON3_6_DICT_KEYS_HEADER_SIZE;
    _numElementsInDictKeys = PYTHON3_6_NUM_ELEMENTS_IN_DICT_KEYS;
    _dictKeysHaveIndex = true;

    if (CheckDictAndStrOffsets(dictForTypeType)) {
      return true;
    }

    _keysInDict = PYTHON3_11_KEYS_IN_DICT;
    _valuesInDict = _keysInDict + sizeof(Offset);
    _dictKeysHeaderSize = PYTHON3_11_DICT_KEYS_HEADER_SIZE;
    _numElementsInDictKeys = PYTHON3_11_NUM_ELEMENTS_IN_DICT_KEYS;
    _dictKeysHaveIndex = true;
    _logarithmicSizeInKeys = true;

    if (CheckDictAndStrOffsets(dictForTypeType)) {
      return true;
    }

    if (_majorVersion == Version3) {
      std::cerr << "Warning: Failed to confirm dict and str offsets for "
                   "python3.\n";
    } else {
      std::cerr << "Warning: Failed to determine offsets for python dict "
                   "and str.\n";
    }
    return false;
  }

  /*
   * Check that the calculated offsets for str work, given that the dict for
   * the type type always contains an str  key "__base__".  If a matching
   * str is found, use this to register the type object for str.
   */
  bool CheckDictAndStrOffsets(Offset dictForTypeType) {
    Reader reader(_virtualAddressMap);
    Offset triples = 0;
    Offset triplesLimit = 0;
    GetTriplesAndLimitFromDict(dictForTypeType, triples, triplesLimit);
    for (Offset triple = triples; triple < triplesLimit;
         triple += 3 * sizeof(Offset)) {
      if (reader.ReadOffset(triple, 0) == 0) {
        continue;
      }
      if (reader.ReadOffset(triple + 2 * sizeof(Offset), 0) == 0) {
        continue;
      }
      Offset strCandidate = reader.ReadOffset(triple + sizeof(Offset), 0);
      if (strCandidate == 0) {
        continue;
      }
      const char* strImage;
      Offset numStrBytesFound =
          _virtualAddressMap.FindMappedMemoryImage(strCandidate, &strImage);
      if (numStrBytesFound < _cstringInStr + 2) {
        continue;
      }
      Offset length = *((Offset*)(strImage + LENGTH_IN_STR));
      if (length != 8) {
        continue;
      }
      if (numStrBytesFound < _cstringInStr + length + 1) {
        continue;
      }
      if (*(strImage + _cstringInStr + length) != 0) {
        continue;
      }
      if (!strcmp("__base__", strImage + _cstringInStr)) {
        _strType = *((Offset*)(strImage + TYPE_IN_PYOBJECT));
        _typeDirectory.RegisterType(_strType, "str");
        return true;
      }
    }
    return false;
  }

  bool CheckGarbageCollectionHeaderSize(Reader& reader, Offset firstEntry,
                                        Offset sizeCandidate) {
    Offset objectType =
        reader.ReadOffset(firstEntry + sizeCandidate + TYPE_IN_PYOBJECT, 0);
    if (objectType != 0 &&
        IsATypeType(reader.ReadOffset(objectType + TYPE_IN_PYOBJECT, 0))) {
      _garbageCollectionHeaderSize = sizeCandidate;
      return true;
    }
    return false;
  }

  void FindNonEmptyGarbageCollectionListsInRange(Offset base, Offset limit,
                                                 Reader& reader,
                                                 Reader& otherReader) {
    Offset listCandidateLimit = limit - 2 * sizeof(Offset);

    for (Offset listCandidate = base; listCandidate < listCandidateLimit;
         listCandidate += sizeof(Offset)) {
      Offset firstEntry = reader.ReadOffset(listCandidate, 0);
      if (firstEntry == 0 || firstEntry == listCandidate) {
        continue;
      }
      if ((otherReader.ReadOffset(firstEntry + sizeof(Offset), 0) &
           ~(sizeof(Offset) - 1)) != listCandidate) {
        continue;
      }
      Offset lastEntry = reader.ReadOffset(listCandidate + sizeof(Offset), 0);
      if (lastEntry == 0 || lastEntry == listCandidate) {
        continue;
      }
      if ((otherReader.ReadOffset(lastEntry, 0) & ~(sizeof(Offset) - 1)) !=
          listCandidate) {
        continue;
      }

      if ((_garbageCollectionHeaderSize == UNKNOWN_OFFSET)
              ? (CheckGarbageCollectionHeaderSize(otherReader, firstEntry,
                                                  2 * sizeof(Offset)) ||
                 CheckGarbageCollectionHeaderSize(otherReader, firstEntry,
                                                  3 * sizeof(Offset)) ||
                 CheckGarbageCollectionHeaderSize(otherReader, firstEntry,
                                                  4 * sizeof(Offset)))
              : CheckGarbageCollectionHeaderSize(
                    otherReader, firstEntry, _garbageCollectionHeaderSize)) {
        if (CheckGarbageCollectionHeaderSize(otherReader, lastEntry,
                                             _garbageCollectionHeaderSize)) {
          if (listCandidate >= limit &&
              CheckGarbageCollectionHeaderSize(otherReader, listCandidate,
                                               _garbageCollectionHeaderSize)) {
            break;
          }
          _nonEmptyGarbageCollectionLists.push_back(listCandidate);
          listCandidate += 2 * sizeof(Offset);
        }
      }
    }
  }
  bool IsPlausiblePyInterpreterState(Offset pyRuntimeStateCandidate,
                                     Offset pyInterpreterStateCandidate,
                                     Reader& reader) {
    if ((pyInterpreterStateCandidate == 0) ||
        ((pyInterpreterStateCandidate & (sizeof(Offset) - 1)) != 0)) {
      return false;
    }
    if (reader.ReadOffset(pyInterpreterStateCandidate + 2 * sizeof(Offset),
                          0xbad) != pyRuntimeStateCandidate) {
      return false;
    }
    Offset pyThreadStateCandidate =
        reader.ReadOffset(pyInterpreterStateCandidate + sizeof(Offset), 0xbad);
    if ((pyThreadStateCandidate == 0) ||
        ((pyThreadStateCandidate & (sizeof(Offset) - 1)) != 0)) {
      return false;
    }
    if (reader.ReadOffset(pyThreadStateCandidate + 2 * sizeof(Offset), 0xbad) !=
        pyInterpreterStateCandidate) {
      return false;
    }

    return true;
  }
  void FindNonEmptyGarbageCollectionListsInPyInterpreterStates(
      Offset base, Offset limit, Reader& reader, Reader& otherReader) {
    Offset pyRuntimeStateCandidateLimit = limit - 8 * sizeof(Offset);
    for (Offset pyRuntimeStateCandidate = base;
         pyRuntimeStateCandidate < pyRuntimeStateCandidateLimit;
         pyRuntimeStateCandidate += sizeof(Offset)) {
      Offset headPyInterpreterStateCandidate = reader.ReadOffset(
          pyRuntimeStateCandidate + 4 * sizeof(int) + 2 * sizeof(Offset),
          0xbad);
      if (!IsPlausiblePyInterpreterState(pyRuntimeStateCandidate,
                                         headPyInterpreterStateCandidate,
                                         otherReader)) {
        continue;
      }
      Offset currentPyInterpreterStateCandidate = reader.ReadOffset(
          pyRuntimeStateCandidate + 4 * sizeof(int) + 3 * sizeof(Offset),
          0xbad);
      Offset link =
          otherReader.ReadOffset(headPyInterpreterStateCandidate, 0xbad);
      if (currentPyInterpreterStateCandidate ==
          headPyInterpreterStateCandidate) {
        if (link != 0) {
          continue;
        }
      } else {
        if (!IsPlausiblePyInterpreterState(pyRuntimeStateCandidate,
                                           currentPyInterpreterStateCandidate,
                                           otherReader)) {
          continue;
        }
        if (link == 0) {
          continue;
        }
        Offset numChecks = 0;
        do {
          if (link != currentPyInterpreterStateCandidate) {
            if (!IsPlausiblePyInterpreterState(pyRuntimeStateCandidate, link,
                                               otherReader)) {
              continue;
            }
          }
          link = otherReader.ReadOffset(link, 0xbad);
        } while ((link != 0) && (++numChecks < 10));
        if (link != 0) {
          continue;
        }
      }
      for (link = headPyInterpreterStateCandidate; link != 0;
           link = otherReader.ReadOffset(link, 0xbad)) {
        Offset base = link + 0x40 * sizeof(Offset);
        Offset limit = link + 0x80 * sizeof(Offset);
        FindNonEmptyGarbageCollectionListsInRange(base, limit, reader,
                                                  otherReader);
      }
      if (!_nonEmptyGarbageCollectionLists.empty()) {
        break;
      }
    }
  }

  void FindNonEmptyGarbageCollectionLists(Offset base, Offset limit,
                                          Reader& reader) {
    Reader otherReader(_virtualAddressMap);

    FindNonEmptyGarbageCollectionListsInRange(base, limit, reader, otherReader);
    if (!_nonEmptyGarbageCollectionLists.empty()) {
      return;
    }

    FindNonEmptyGarbageCollectionListsInPyInterpreterStates(base, limit, reader,
                                                            otherReader);
    if (!_nonEmptyGarbageCollectionLists.empty()) {
      return;
    }

    std::cerr << "Warning: No non-empty Python garbage collection lists were "
                 "found.\n"
              << "   The counts for %ContainerPythonObject are likely to be "
                 "incorrectly low.\n";
  }
};
}  // namespace Python
}  // namespace chap
