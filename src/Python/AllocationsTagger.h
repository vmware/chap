// Copyright (c) 2019-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
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
  AllocationsTagger(const Allocations::Graph<Offset>& graph,
                    TagHolder& tagHolder,
                    const InfrastructureFinder<Offset>& infrastructureFinder,
                    const VirtualAddressMap<Offset>& virtualAddressMap)
      : _graph(graph),
        _directory(graph.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _tagHolder(tagHolder),
        _infrastructureFinder(infrastructureFinder),
        _arenaStructArray(infrastructureFinder.ArenaStructArray()),
        _typeType(infrastructureFinder.TypeType()),
        _dictType(infrastructureFinder.DictType()),
        _keysInDict(infrastructureFinder.KeysInDict()),
        _nonEmptyGarbageCollectionLists(
            infrastructureFinder.NonEmptyGarbageCollectionLists()),
        _garbageCollectionHeaderSize(
            infrastructureFinder.GarbageCollectionHeaderSize()),
        _cachedKeysInHeapTypeObject(
            infrastructureFinder.CachedKeysInHeapTypeObject()),
        _virtualAddressMap(virtualAddressMap),
        _reader(_virtualAddressMap),
        _simplePythonObjectTagIndex(
            _tagHolder.RegisterTag("%SimplePythonObject")),
        _containerPythonObjectTagIndex(
            _tagHolder.RegisterTag("%ContainerPythonObject")),
        _dictKeysObjectTagIndex(_tagHolder.RegisterTag("%PyDictKeysObject")),
        _arenaStructArrayTagIndex(
            _tagHolder.RegisterTag("%PythonArenaStructArray")),
        _mallocedArenaTagIndex(_tagHolder.RegisterTag("%PythonMallocedArena")),
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
            !TagAsUntrackedContainerPythonObject(contiguousImage, index)) {
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

 private:
  const Allocations::Graph<Offset>& _graph;
  const Allocations::Directory<Offset>& _directory;
  const AllocationIndex _numAllocations;
  TagHolder& _tagHolder;
  const InfrastructureFinder<Offset> _infrastructureFinder;
  const Offset _arenaStructArray;
  const Offset _typeType;
  const Offset _dictType;
  const Offset _keysInDict;
  const std::vector<Offset> _nonEmptyGarbageCollectionLists;
  const Offset _garbageCollectionHeaderSize;
  const Offset _cachedKeysInHeapTypeObject;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  Reader _reader;
  TagIndex _simplePythonObjectTagIndex;
  TagIndex _containerPythonObjectTagIndex;
  TagIndex _dictKeysObjectTagIndex;
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
      /*
       * The most common case is that the python arenas are all
       * allocated by mmap() but it is possible, based on an #ifdef
       * that the arenas can be allocated via malloc().  This checks
       * for the latter case in a way that favors the former; if no
       * arenas are malloced, there will not be any outgoing references
       * from the array of arena structures.
       */
      for (const AllocationIndex* pNextOutgoing = pFirstOutgoing;
           pNextOutgoing != pPastOutgoing; pNextOutgoing++) {
        AllocationIndex arenaCandidateIndex = *pNextOutgoing;
        Offset arenaCandidate =
            _directory.AllocationAt(arenaCandidateIndex)->Address();
        if (_infrastructureFinder.ArenaStructFor(arenaCandidate) != 0) {
          _tagHolder.TagAllocation(arenaCandidateIndex, _mallocedArenaTagIndex);
        }
      }
      return true;
    }
    return false;
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
              }
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

    if (offsetLimit - offsets >= 2 &&
        _reader.ReadOffset(
            offsets[1] + InfrastructureFinder<Offset>::TYPE_IN_PYOBJECT, ~0) ==
            _typeType) {
      _tagHolder.TagAllocation(index, _simplePythonObjectTagIndex);
      return true;
    }
    return false;
  }

  /*
  * Check if the allocation contains a garbage collection header for
  * an untracked python object followed by a PyObject and tag it
  * as a ContainerPythonObject if so.
  */

  bool TagAsUntrackedContainerPythonObject(
      const ContiguousImage& contiguousImage, AllocationIndex index) {
    const char* firstChar = contiguousImage.FirstChar();
    Offset size = contiguousImage.Size();
    if (size >= _garbageCollectionHeaderSize + 2 * sizeof(Offset) &&
        ((_garbageCollectionHeaderSize == 2 * sizeof(Offset)) ||
         (*((Offset*)(firstChar + 2 * sizeof(Offset))) & ((Offset)(~7))) ==
             ((Offset)(~7)))) {
      Offset typeCandidate =
          *((Offset*)(firstChar + _garbageCollectionHeaderSize +
                      InfrastructureFinder<Offset>::TYPE_IN_PYOBJECT));
      if (typeCandidate == _dictType ||
          (*((Offset*)(firstChar)) == 0 &&
           _infrastructureFinder.HasType(typeCandidate))) {
        _tagHolder.TagAllocation(index, _containerPythonObjectTagIndex);
        if (typeCandidate == _dictType &&
            size >=
                _garbageCollectionHeaderSize + _keysInDict + sizeof(Offset)) {
          Offset keysAddr = *((
              Offset*)(firstChar + _garbageCollectionHeaderSize + _keysInDict));
          AllocationIndex keysIndex = _directory.AllocationIndexOf(keysAddr);
          if (keysIndex != _numAllocations && keysIndex != index) {
            _tagHolder.TagAllocation(keysIndex, _dictKeysObjectTagIndex);
          }
        }
        return true;
      }
    }
    return false;
  }
};
}  // namespace Python
}  // namespace chap
