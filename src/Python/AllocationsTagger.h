// Copyright (c) 2019-2025 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>

#include "../Allocations/EdgePredicate.h"
#include "../Allocations/Graph.h"
#include "../Allocations/TagHolder.h"
#include "../Allocations/Tagger.h"
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace Python {
template <typename Offset>
class AllocationsTagger : public Allocations::Tagger<Offset> {
 public:
  typedef typename Allocations::Graph<Offset>::EdgeIndex EdgeIndex;
  typedef typename Allocations::Directory<Offset> Directory;
  typedef typename Allocations::Tagger<Offset> Tagger;
  typedef typename Tagger::Phase Phase;
  typedef typename Directory::AllocationIndex AllocationIndex;
  typedef typename Directory::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename TagHolder::TagIndex TagIndex;
  typedef typename Allocations::EdgePredicate<Offset> EdgePredicate;
  AllocationsTagger(const Allocations::Graph<Offset>& graph,
                    TagHolder& tagHolder, EdgePredicate& edgeIsTainted,
                    EdgePredicate& edgeIsFavored,
                    const InfrastructureFinder<Offset>& infrastructureFinder,
                    const VirtualAddressMap<Offset>& virtualAddressMap)
      : _graph(graph),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _tagHolder(tagHolder),
        _edgeIsTainted(edgeIsTainted),
        _edgeIsFavored(edgeIsFavored),
        _infrastructureFinder(infrastructureFinder),
        _arenaStructArray(infrastructureFinder.ArenaStructArray()),
        _arenaSize(infrastructureFinder.ArenaSize()),
        _poolSize(infrastructureFinder.PoolSize()),
        _typeType(infrastructureFinder.TypeType()),
        _dictType(infrastructureFinder.DictType()),
        _keysInDict(infrastructureFinder.KeysInDict()),
        _valuesInDict(infrastructureFinder.ValuesInDict()),
        _listType(infrastructureFinder.ListType()),
        _itemsInList(infrastructureFinder.ItemsInList()),
        _strType(infrastructureFinder.StrType()),
        _intType(infrastructureFinder.IntType()),
        _bytesType(infrastructureFinder.BytesType()),
        _floatType(infrastructureFinder.FloatType()),
        _setType(infrastructureFinder.SetType()),
        _frozensetType(infrastructureFinder.FrozenSetType()),
        _dequeType(infrastructureFinder.DequeType()),
        _firstBlockInDeque(infrastructureFinder.FirstBlockInDeque()),
        _lastBlockInDeque(infrastructureFinder.LastBlockInDeque()),
        _forwardInDequeBlock(infrastructureFinder.ForwardInDequeBlock()),
        _nonEmptyGarbageCollectionLists(
            infrastructureFinder.NonEmptyGarbageCollectionLists()),
        _garbageCollectionHeaderSize(
            infrastructureFinder.GarbageCollectionHeaderSize()),
        _cachedKeysInHeapTypeObject(
            infrastructureFinder.CachedKeysInHeapTypeObject()),
        _virtualAddressMap(virtualAddressMap),
        _reader(_virtualAddressMap),
        _simplePythonObjectTagIndex(
            _tagHolder.RegisterTag("%SimplePythonObject", true, true)),
        _containerPythonObjectTagIndex(
            _tagHolder.RegisterTag("%ContainerPythonObject", true, true)),
        _dictKeysObjectTagIndex(
            _tagHolder.RegisterTag("%PyDictKeysObject", true, true)),
        _dictValuesArrayTagIndex(
            _tagHolder.RegisterTag("%PyDictValuesArray", true, true)),
        _setEntryArrayTagIndex(
            _tagHolder.RegisterTag("%PySetEntryArray", true, true)),
        _listItemsTagIndex(
            _tagHolder.RegisterTag("%PythonListItems", true, true)),
        _dequeBlockTagIndex(
            _tagHolder.RegisterTag("%PythonDequeBlock", true, true)),
        _arenaStructArrayTagIndex(
            _tagHolder.RegisterTag("%PythonArenaStructArray", true, false)),
        _mallocedArenaTagIndex(
            _tagHolder.RegisterTag("%PythonMallocedArena", true, true)),
        _enabled(_arenaStructArray != 0) {
    TagListedContainerPythonObjects();
  }

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    if (!_enabled) {
      return true;  // There is nothing more to check.
    }
    if (_tagHolder.IsStronglyTagged(index)) {
      // This allocation was already strongly tagged as something else.
      return true;  // We are finished looking at this allocation.
    }

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        if (!TagAsArenaStructArray(index, allocation) &&
            !TagAsUntrackedContainerPythonObject(contiguousImage, index,
                                                 allocation.Address())) {
          TagAsSimplePythonObject(contiguousImage, index);
        }
        /*
         * All the checks are done in the first phase because they are
         * inexpensive.
         */
        return true;
        break;
      case Tagger::MEDIUM_CHECK:
        // Sublinear if reject, match must be solid
        break;
      case Tagger::SLOW_CHECK:
        // May be expensive, match must be solid
        break;
      case Tagger::WEAK_CHECK:
        // May be expensive, weak results OK
        // An example here might be if one of the nodes in the chain is
        // no
        // longer allocated.
        break;
    }
    return false;
  }

  void MarkFavoredReferences(const ContiguousImage& contiguousImage,
                             Reader& /* reader */, AllocationIndex index,
                             const Allocation& /* allocation */,
                             const EdgeIndex* outgoingEdgeIndices) {
    const Offset* checkLimit = contiguousImage.OffsetLimit();
    const Offset* firstCheck = contiguousImage.FirstOffset();

    for (const Offset* check = firstCheck; check < checkLimit; check++) {
      EdgeIndex edgeIndex = outgoingEdgeIndices[check - firstCheck];
      AllocationIndex targetIndex = _graph.GetTargetForOutgoing(edgeIndex);
      if (targetIndex == _numAllocations) {
        continue;
      }
      TagIndex tagIndex = _tagHolder.GetTagIndex(targetIndex);
      Offset targetAddress = _directory.AllocationAt(targetIndex)->Address();
      if ((tagIndex == _simplePythonObjectTagIndex &&
           targetAddress == *check) ||
          (tagIndex == _containerPythonObjectTagIndex &&
           (targetAddress + _garbageCollectionHeaderSize) == *check)) {
        _edgeIsFavored.Set(index, targetIndex, true);
      }
    }
  }

 private:
  const Allocations::Graph<Offset>& _graph;
  const Allocations::Directory<Offset>& _directory;
  const AllocationIndex _numAllocations;
  TagHolder& _tagHolder;
  EdgePredicate& _edgeIsTainted;
  EdgePredicate& _edgeIsFavored;
  const InfrastructureFinder<Offset> _infrastructureFinder;
  const Offset _arenaStructArray;
  const Offset _arenaSize;
  const Offset _poolSize;
  const Offset _typeType;
  const Offset _dictType;
  const Offset _keysInDict;
  const Offset _valuesInDict;
  const Offset _listType;
  const Offset _itemsInList;
  const Offset _strType;
  const Offset _intType;
  const Offset _bytesType;
  const Offset _floatType;
  const Offset _setType;
  const Offset _frozensetType;
  const Offset _dequeType;
  const Offset _firstBlockInDeque;
  const Offset _lastBlockInDeque;
  const Offset _forwardInDequeBlock;
  const std::vector<Offset> _nonEmptyGarbageCollectionLists;
  const Offset _garbageCollectionHeaderSize;
  const Offset _cachedKeysInHeapTypeObject;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  Reader _reader;
  TagIndex _simplePythonObjectTagIndex;
  TagIndex _containerPythonObjectTagIndex;
  TagIndex _dictKeysObjectTagIndex;
  TagIndex _dictValuesArrayTagIndex;
  TagIndex _setEntryArrayTagIndex;
  TagIndex _listItemsTagIndex;
  TagIndex _dequeBlockTagIndex;
  TagIndex _arenaStructArrayTagIndex;
  TagIndex _mallocedArenaTagIndex;
  bool _enabled;

  /*
   * Check if the given allocation contains the ArenaStructArray, returning
   * true only if so.  If this is an ArenaStructArray tag any referenced
   * arenas that were malloced (as opposed to mmapped).
   */
  bool TagAsArenaStructArray(AllocationIndex index,
                             const Allocation& allocation) {
    if (allocation.Address() == _arenaStructArray) {
      _tagHolder.TagAllocation(index, _arenaStructArrayTagIndex);
      const AllocationIndex* pFirstOutgoing;
      const AllocationIndex* pPastOutgoing;
      _graph.GetOutgoing(index, &pFirstOutgoing, &pPastOutgoing);
      for (const AllocationIndex* pNextOutgoing = pFirstOutgoing;
           pNextOutgoing != pPastOutgoing; pNextOutgoing++) {
        /*
         * References between allocations are always to the inner-most
         * allocation that contains the referenced address.  The start
         * of an allocation may or may not be the start of a pool, but
         * the start of a pool is not the start of some block within
         * the pool because each pool has a header.
         */
        AllocationIndex arenaCandidateIndex = *pNextOutgoing;
        const Allocation* allocation =
            _directory.AllocationAt(arenaCandidateIndex);
        Offset arenaCandidate = allocation->Address();
        Offset allocationSize = allocation->Size();

        if (allocationSize == _poolSize) {
          if (!allocation->IsWrapped()) {
            continue;
          }
          Offset firstPoolAddress = arenaCandidate;
          allocation = _directory.AllocationAt(--arenaCandidateIndex);
          arenaCandidate = allocation->Address();
          Offset allocationSize = allocation->Size();
          if (arenaCandidate + allocationSize < firstPoolAddress + _poolSize) {
            continue;
          }
        }
        if (allocationSize >= _arenaSize) {
          if (_infrastructureFinder.ArenaStructFor(arenaCandidate) != 0) {
            _tagHolder.TagAllocation(arenaCandidateIndex,
                                     _mallocedArenaTagIndex);
            _edgeIsFavored.Set(index, arenaCandidateIndex, true);
          }
        }
      }
      return true;
    }
    return false;
  }

  void TagDequeBlocks(AllocationIndex dequeIndex, Offset dequeAllocation) {
    Reader reader(_virtualAddressMap);
    Offset dequeStart = dequeAllocation + _garbageCollectionHeaderSize;
    Offset firstDequeBlock =
        reader.ReadOffset(dequeStart + _firstBlockInDeque, 0xbad);
    if (firstDequeBlock == 0xbad) {
      std::cerr << "Warning: unable to get first block address for deque at 0x"
                << std::hex << dequeAllocation << "\n";
      return;
    }
    Offset lastDequeBlock =
        reader.ReadOffset(dequeStart + _lastBlockInDeque, 0xbad);
    if (lastDequeBlock == 0xbad) {
      std::cerr << "Warning: unable to get last block address for deque at 0x"
                << std::hex << dequeAllocation << "\n";
      return;
    }
    AllocationIndex dequeBlocksSeen = 0;
    Offset dequeBlock = firstDequeBlock;
    AllocationIndex prevIndex = dequeIndex;
    while (dequeBlocksSeen++ < _numAllocations) {
      AllocationIndex dequeBlockIndex =
          _directory.AllocationIndexOf(dequeBlock);
      if (dequeBlockIndex == _numAllocations) {
        break;
      }
      _tagHolder.TagAllocation(dequeBlockIndex, _dequeBlockTagIndex);
      _edgeIsFavored.Set(prevIndex, dequeBlockIndex, true);
      prevIndex = dequeBlockIndex;
      if (dequeBlock == lastDequeBlock) {
        break;
      }
      dequeBlock = reader.ReadOffset(dequeBlock + _forwardInDequeBlock, 0xbad);
      if (dequeBlock == 0) {
        break;
      }
      if (dequeBlock == 0xbad) {
        std::cerr
            << "Warning: unable to access full chain of blocks for deque at 0x"
            << std::hex << dequeAllocation << "\n";
        break;
      }
    }
  }

  /*
   * Tag all the container python objects that appear on one of the garbage
   * collection lists.  It is necessary to do this eagerly because such
   * objects happen to match the %ListNode pattern but the pattern for
   * container python objects is a bit stronger.
   */
  void TagListedContainerPythonObjects() {
    Reader reader(_virtualAddressMap);
    for (auto listHead : _nonEmptyGarbageCollectionLists) {
      Offset prevNode = listHead;
      for (Offset node =
               reader.ReadOffset(listHead, listHead) & ~(sizeof(Offset) - 1);
           node != listHead;
           node = reader.ReadOffset(node, 0) & ~(sizeof(Offset) - 1)) {
        if (node == 0) {
          break;
        }
        if ((reader.ReadOffset(node + sizeof(Offset), 0) &
             ~(sizeof(Offset) - 1)) != prevNode) {
          // The list is corrupt, but this has already been reported.
          break;
        }
        prevNode = node;
        Offset typeCandidate = reader.ReadOffset(
            node + _garbageCollectionHeaderSize +
                InfrastructureFinder<Offset>::TYPE_IN_PYOBJECT,
            ~0);
        if (_infrastructureFinder.HasType(typeCandidate)) {
          // It is expected that each list entry contains a garbage collection
          // header followed by a type object.  The check is here in case there
          // is corruption in the the list but there is no need to report
          // because errors were reported when the list was processed to find
          // types.
          AllocationIndex index = _directory.AllocationIndexOf(node);
          if (index == _numAllocations) {
            std::cerr << "Warining: GC list contains a non-allocation at 0x"
                      << std::hex << node << "\n";
            break;
          } else {
            _tagHolder.TagAllocation(index, _containerPythonObjectTagIndex);
            if (typeCandidate == _dictType) {
              Offset keysAddr = reader.ReadOffset(
                  node + _garbageCollectionHeaderSize + _keysInDict, ~0);
              AllocationIndex keysIndex =
                  _directory.AllocationIndexOf(keysAddr);
              if (keysIndex != _numAllocations && keysIndex != index) {
                _tagHolder.TagAllocation(keysIndex, _dictKeysObjectTagIndex);
                _edgeIsFavored.Set(index, keysIndex, true);
              }
              if (_valuesInDict !=
                  InfrastructureFinder<Offset>::UNKNOWN_OFFSET) {
                Offset valuesAddr = reader.ReadOffset(
                    node + _garbageCollectionHeaderSize + _valuesInDict, ~0);
                AllocationIndex valuesIndex =
                    _directory.AllocationIndexOf(valuesAddr);
                if (valuesIndex != _numAllocations && valuesIndex != index) {
                  _tagHolder.TagAllocation(valuesIndex,
                                           _dictValuesArrayTagIndex);
                  _edgeIsFavored.Set(index, valuesIndex, true);
                }
              }
            } else if (typeCandidate == _setType ||
                       typeCandidate == _frozensetType) {
              Offset mask = reader.ReadOffset(
                  node + _garbageCollectionHeaderSize +
                      InfrastructureFinder<Offset>::MASK_IN_PYSETOBJECT,
                  0);
              if (((mask + 1) & mask) != 0) {
                std::cerr << "Warning: Python set or frozenset allocation at 0x"
                          << std::hex << node << " has unexpected mask 0x"
                          << mask << ".\n";
                continue;
              }
              Offset table = reader.ReadOffset(
                  node + _garbageCollectionHeaderSize +
                      InfrastructureFinder<Offset>::TABLE_IN_PYSETOBJECT,
                  0);
              if (mask == InfrastructureFinder<Offset>::MIN_PYSETOBJECT_MASK) {
              } else {
                AllocationIndex tableIndex =
                    _directory.AllocationIndexOf(table);
                if (tableIndex == _numAllocations) {
                  std::cerr
                      << "Warning: Python set or frozenset allocation at 0x"
                      << std::hex << node << " has unexpected table 0x" << table
                      << ".\n";
                  continue;
                }
                _tagHolder.TagAllocation(tableIndex, _setEntryArrayTagIndex);
                // All edges except the one to the entry array are considered
                // tainted.  For example there may be stale references from
                // small_table.
                _edgeIsTainted.SetAllOutgoing(index, true);
                _edgeIsTainted.Set(index, tableIndex, false);
                // The reference via the table field is considered favored
                // as it holds the entry array.
                _edgeIsFavored.Set(index, tableIndex, true);
              }
            } else if (typeCandidate == _listType) {
              Offset itemsAddr = reader.ReadOffset(
                  node + _garbageCollectionHeaderSize + _itemsInList, ~0);
              AllocationIndex itemsIndex =
                  _directory.AllocationIndexOf(itemsAddr);
              if (itemsIndex != _numAllocations && itemsIndex != index) {
                _tagHolder.TagAllocation(itemsIndex, _listItemsTagIndex);
                _edgeIsFavored.Set(index, itemsIndex, true);
              }
            } else if (typeCandidate == _dequeType) {
              TagDequeBlocks(index, node);
            } else if (_infrastructureFinder.IsATypeType(typeCandidate) &&
                       _cachedKeysInHeapTypeObject !=
                           InfrastructureFinder<Offset>::UNKNOWN_OFFSET) {
              Offset keysAddr =
                  reader.ReadOffset(node + _garbageCollectionHeaderSize +
                                        _cachedKeysInHeapTypeObject,
                                    ~0);
              AllocationIndex keysIndex =
                  _directory.AllocationIndexOf(keysAddr);
              if (keysIndex != _numAllocations && keysIndex != index) {
                _tagHolder.TagAllocation(keysIndex, _dictKeysObjectTagIndex);
                _edgeIsFavored.Set(index, keysIndex, true);
              }
            }
          }
        } else {
          std::cerr << "Warning: GC list at 0x" << std::hex << listHead
                    << " has a node at 0x" << node
                    << "\nthat does not contain a typed object or has "
                       "questionable type 0x"
                    << typeCandidate << ".\n";
        }
      }
    }
  }

  /*
   * Check if the allocation contains a PyObject at the start and
   * tag it as a SimplePythonObject if so.
   */

  bool TagAsSimplePythonObject(const ContiguousImage& contiguousImage,
                               AllocationIndex index) {
    const Offset* offsetLimit = contiguousImage.OffsetLimit();
    const Offset* offsets = contiguousImage.FirstOffset();

    if (_typeType != 0 && offsetLimit - offsets >= 2) {
      Offset typeCandidate = offsets[1];
      if (typeCandidate != 0 &&
          _infrastructureFinder.IsATypeType(_reader.ReadOffset(
              typeCandidate + InfrastructureFinder<Offset>::TYPE_IN_PYOBJECT,
              ~0))) {
        _tagHolder.TagAllocation(index, _simplePythonObjectTagIndex);
        if (typeCandidate == _intType || typeCandidate == _floatType ||
            typeCandidate == _bytesType || typeCandidate == _strType) {
          _edgeIsTainted.SetAllOutgoing(index, true);
        }
        return true;
      }
    }
    return false;
  }

  /*
   * Check if the allocation contains a garbage collection header for
   * an untracked python object followed by a PyObject and tag it
   * as a ContainerPythonObject if so.
   */

  bool TagAsUntrackedContainerPythonObject(
      const ContiguousImage& contiguousImage, AllocationIndex index,
      Offset allocationAddress) {
    const char* firstChar = contiguousImage.FirstChar();
    Offset size = contiguousImage.Size();
    if (size >= _garbageCollectionHeaderSize + 2 * sizeof(Offset) &&
        ((_garbageCollectionHeaderSize == 2 * sizeof(Offset)) ||
         (*((Offset*)(firstChar + 2 * sizeof(Offset))) & ((Offset)(~7))) ==
             ((Offset)(~7)))) {
      Offset typeCandidate =
          *((Offset*)(firstChar + _garbageCollectionHeaderSize +
                      InfrastructureFinder<Offset>::TYPE_IN_PYOBJECT));
      if (typeCandidate == 0) {
        return false;
      }
      if (typeCandidate == _dictType || typeCandidate == _listType ||
          typeCandidate == _dequeType ||
          ((*((Offset*)(firstChar)) == 0) &&
           _infrastructureFinder.HasType(typeCandidate))) {
        _tagHolder.TagAllocation(index, _containerPythonObjectTagIndex);
        if (typeCandidate == _dictType) {
          if (size >=
              _garbageCollectionHeaderSize + _keysInDict + sizeof(Offset)) {
            Offset keysAddr =
                *((Offset*)(firstChar + _garbageCollectionHeaderSize +
                            _keysInDict));
            AllocationIndex keysIndex = _directory.AllocationIndexOf(keysAddr);
            if (keysIndex != _numAllocations && keysIndex != index) {
              _tagHolder.TagAllocation(keysIndex, _dictKeysObjectTagIndex);
              _edgeIsFavored.Set(index, keysIndex, true);
            }
          }
          if (_valuesInDict != InfrastructureFinder<Offset>::UNKNOWN_OFFSET) {
            if (size >=
                _garbageCollectionHeaderSize + _valuesInDict + sizeof(Offset)) {
              Offset valuesAddr =
                  *((Offset*)(firstChar + _garbageCollectionHeaderSize +
                              _valuesInDict));
              AllocationIndex valuesIndex =
                  _directory.AllocationIndexOf(valuesAddr);
              if (valuesIndex != _numAllocations && valuesIndex != index) {
                _tagHolder.TagAllocation(valuesIndex, _dictValuesArrayTagIndex);
                _edgeIsFavored.Set(index, valuesIndex, true);
              }
            }
          }
        } else if (typeCandidate == _listType) {
          if (size >=
              _garbageCollectionHeaderSize + _itemsInList + sizeof(Offset)) {
            Offset itemsAddr =
                *((Offset*)(firstChar + _garbageCollectionHeaderSize +
                            _itemsInList));
            AllocationIndex itemsIndex =
                _directory.AllocationIndexOf(itemsAddr);
            if (itemsIndex != _numAllocations && itemsIndex != index) {
              _tagHolder.TagAllocation(itemsIndex, _listItemsTagIndex);
              _edgeIsFavored.Set(index, itemsIndex, true);
            }
          }
        } else if (typeCandidate == _dequeType) {
          TagDequeBlocks(index, allocationAddress);
        }
        return true;
      }
    }
    return false;
  }
};
}  // namespace Python
}  // namespace chap
