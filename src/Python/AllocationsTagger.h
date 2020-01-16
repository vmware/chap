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
  typedef typename Allocations::Finder<Offset> Finder;
  typedef typename Allocations::Tagger<Offset> Tagger;
  typedef typename Tagger::Phase Phase;
  typedef typename Finder::AllocationIndex AllocationIndex;
  typedef typename Finder::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename TagHolder::TagIndex TagIndex;
  AllocationsTagger(const Allocations::Graph<Offset>& graph,
                    TagHolder& tagHolder,
                    const ModuleDirectory<Offset>& moduleDirectory,
                    const InfrastructureFinder<Offset>& infrastructureFinder)
      : _graph(graph),
        _finder(graph.GetAllocationFinder()),
        _tagHolder(tagHolder),
        _infrastructureFinder(infrastructureFinder),
        _arenaStructArray(infrastructureFinder.ArenaStructArray()),
        _dictKeysObjectTagIndex(_tagHolder.RegisterTag("%PyDictKeysObject")),
        _arenaStructArrayTagIndex(
            _tagHolder.RegisterTag("%PythonArenaStructArray")),
        _mallocedArenaTagIndex(_tagHolder.RegisterTag("%PythonMallocedArena")),
        _rangeToFlags(nullptr),
        _candidateBase(0),
        _candidateLimit(0),
        _enabled(_arenaStructArray != 0) {
    const std::string& libraryPath = _infrastructureFinder.LibraryPath();
    _rangeToFlags = moduleDirectory.Find(libraryPath);
    if (libraryPath.find("python3") != std::string::npos) {
      /*
       * This, and most of the logic to tag PyDictKeysObject will change
       * when more general recognition of python types has been provided.
       */
      _candidateBase = _infrastructureFinder.LibraryBase();
      _candidateLimit = _infrastructureFinder.LibraryLimit();
    }
  }

  bool TagFromAllocation(const ContiguousImage& contiguousImage,
                         Reader& /* reader */, AllocationIndex index,
                         Phase phase, const Allocation& allocation,
                         bool /* isUnsigned */) {
    if (!_enabled) {
      return true;  // There is nothing more to check.
    }
    if (_tagHolder.GetTagIndex(index) != 0) {
      /*
       * This was already tagged, generally as a result of following
       * outgoing references from an allocation already being tagged.
       * From this we conclude that the given allocation is not the root
       * node for a map or set.
       */
      return true;  // We are finished looking at this allocation for this pass.
    }

    switch (phase) {
      case Tagger::QUICK_INITIAL_CHECK:
        // Fast initial check, match must be solid
        {
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
                  _finder.AllocationAt(arenaCandidateIndex)->Address();
              if (_infrastructureFinder.ArenaStructFor(arenaCandidate) != 0) {
                _tagHolder.TagAllocation(arenaCandidateIndex,
                                         _mallocedArenaTagIndex);
              }
            }
            return true;
          }
          const Offset* offsetLimit = contiguousImage.OffsetLimit();
          const Offset* offsets = contiguousImage.FirstOffset();
          if (offsetLimit - offsets < 5) {
            return true;
          }
          if (offsets[0] != 1) {
            /*
             * This is supposed to be a reference count of 1 because the
             * given object is considered to be exclusively owned.
             */
            return true;
          }
          Offset numSlots = offsets[1];
          if ((numSlots ^ (numSlots - 1)) != 2 * numSlots - 1) {
            // The number of slots must be a power of 2.
            return true;
          }

          if ((Offset)(((offsetLimit - offsets) - 5) / 3) < numSlots) {
            /*
             * The object wouldn't fit in the allocation.
             */
            return true;
          }
          Offset method = offsets[2];
          if (_candidateBase > method || method >= _candidateLimit) {
            return true;
          }

          if (_methods.find(method) == _methods.end()) {
            typename ModuleDirectory<Offset>::RangeToFlags::const_iterator it =
                _rangeToFlags->find(method);
            if (it == _rangeToFlags->end()) {
              return true;
            }
            int flags = it->_value;
            if ((flags &
                 (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE |
                  RangeAttributes::IS_EXECUTABLE)) !=
                (RangeAttributes::IS_READABLE |
                 RangeAttributes::IS_EXECUTABLE)) {
              return true;
            }
            _methods.insert(method);
          }
          _tagHolder.TagAllocation(index, _dictKeysObjectTagIndex);
          return true;  // No more checking is needed
        }
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
  const Allocations::Finder<Offset>& _finder;
  TagHolder& _tagHolder;
  const InfrastructureFinder<Offset> _infrastructureFinder;
  const Offset _arenaStructArray;
  TagIndex _dictKeysObjectTagIndex;
  TagIndex _arenaStructArrayTagIndex;
  TagIndex _mallocedArenaTagIndex;
  const typename ModuleDirectory<Offset>::RangeToFlags* _rangeToFlags;
  Offset _candidateBase;
  Offset _candidateLimit;
  bool _enabled;

  std::set<Offset> _methods;
};
}  // namespace Python
}  // namespace chap
