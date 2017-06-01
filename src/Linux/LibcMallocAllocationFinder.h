// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Finder.h"
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace chap {
namespace Linux {
template <class Offset>
class LibcMallocAllocationFinder : public Allocations::Finder<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;

  LibcMallocAllocationFinder(
      VirtualMemoryPartition<Offset>& virtualMemoryPartition)
      : Allocations::Finder<Offset>(virtualMemoryPartition.GetAddressMap()),
        LIBC_MALLOC_HEAP("libc malloc heap"),
        LIBC_MALLOC_MAIN_ARENA("libc malloc main arena"),
        LIBC_MALLOC_MAIN_ARENA_PAGES("libc malloc main arena pages"),
        LIBC_MALLOC_LARGE_ALLOCATIONS("libc malloc large allocations"),
        _virtualMemoryPartition(virtualMemoryPartition),
        _addressMap(virtualMemoryPartition.GetAddressMap()),
        _mainArenaAddress(0),
        _mainArenaIsContiguous(false),
        _maxHeapSize(DEFAULT_MAX_HEAP_SIZE) {
    FindHeapAndArenaCandidates();

    if (_arenas.size() == 0) {
      /*
       * No non-main arenas were found.  It is possible that there really
       * is just the main arena.  In any case we can scan for it.  It is
       * also possible that someone has overridden the default max heap
       * size at the time glibc was compiled.
       */
      if (ScanForMainArena()) {
        /*
         * The main arena was found.  See if it points to itself, in which
         * case there really is just one arena, or it points to something
         * that looks like a heap, in which case someone has probably
         * reduced the default max heap size at compilation time, causing
         * the heaps not to be detected based on the default.
         * At present no attempt is made to handle the corner case
         * of a non-standard maximum heap size (which might cause the
         * initial attempt to scan for heaps to failed) coupled with
         * an incomplete core (which could cause the following call
         * to fail if one of the headers from the ring were not present).
         */
        if (!FindNonMainArenasByRingFromMainArena()) {
          DeriveArenaOffsets();
        }
      } else {
        std::cerr << "Failed to find any arenas, main or not."
                  << std::endl;
      }

    } else {
      /*
       * At least one non-main arena is present.  That means, if the
       * core is complete, that we expect to find a ring containing
       * at least two arenas, one of which was the main arena.
       */

      bool foundArenasByRing = FindArenasByRingFromNonMainArenas();
      if (!foundArenasByRing) {
        /*
         * It was not possible to complete the ring, least not
         * based on the default maximum heap size.
         */

        if (ScanForMainArena()) {
          /*
           * The main arena was found.  Perhaps someone reduced the
           * default max heap size at compilation time.  Check that.
           */
          foundArenasByRing = FindNonMainArenasByRingFromMainArena();
        }
      }
      if (!foundArenasByRing) {
        /*
         * It was not possible to correct the set of arenas and heaps
         * by finding an arena ring.  Offsets are not yet known yet
         * because they would only be set at this point if all the
         * arenas had been found.  The main arena is often found
         * during this derivation but may not if the arena that
         * refers to the main arena is missing from the core.
         * An incomplete arena ring will also prevent any checking
         * for non-standard maximum heap size values.
         */

        bool hadMainArenaBeforeDerivation = (_mainArenaAddress != 0);

        DeriveArenaOffsets();

        if (_mainArenaAddress != 0 && !hadMainArenaBeforeDerivation) {
          Reader reader(_addressMap);
          try {
            Offset nextArena =
                reader.ReadOffset(_mainArenaAddress + _arenaNextOffset);
            Offset top = reader.ReadOffset(_mainArenaAddress + _arenaTopOffset);
            Offset size =
                reader.ReadOffset(_mainArenaAddress + _arenaSizeOffset);
            bool isContiguous =
                (reader.ReadU32(nextArena + sizeof(uint32_t)) & 2) == 0;
            ArenaMapIterator itMainArena =
                _arenas
                    .insert(std::make_pair(_mainArenaAddress,
                                           Arena(_mainArenaAddress)))
                    .first;
            Arena& mainArena = itMainArena->second;
            mainArena._nextArena = nextArena;
            mainArena._top = top;
            mainArena._size = size;
            _mainArenaIsContiguous = isContiguous;
          } catch (NotMapped&) {
            std::cerr << "Derived main arena address at " << std::hex
                      << _mainArenaAddress << " appears to be suspect."
                      << std::endl;
            std::cerr << "One possibility is an incomplete core." << std::endl;
          }
          /*
           * For detected heaps that do not refer to valid non-main arenas
           * do further checking to see whether the issue is an arena that
           * is missing from the core or whether the heap is actually
           * appears to be invalid.  The count can't go to 0 because
           * there is at least one valid heap per detected non-main arena.
           */

          CheckHeapArenaReferences();

          CheckArenaTops();

          /*
           * Report any issues with the arena nexts.
           */

          CheckArenaNexts();
        }
      }
    }

    HeapMapConstIterator itHeapsEnd = _heaps.end();
    for (HeapMapConstIterator itHeaps = _heaps.begin(); itHeaps != itHeapsEnd;
         ++itHeaps) {
      _virtualMemoryPartition.ClaimRange(itHeaps->first, _maxHeapSize,
                                         LIBC_MALLOC_HEAP);
    }

    if (_mainArenaAddress != 0) {
      /*
       * It is necessary to claim the arena itself to avoid any false
       * anchors from bin and fast bin pointers in the main arena.  The
       * issue is that when libc malloc points to individual allocations,
       * it actually points to the last sizeof(size_t) bytes of the
       * preceding allocation, which would then be interpreted as edges
       * from the main arena to the preceding allocation.
       *
       * Note that the calculation of the main arena limit is approximate
       * but sufficiently accurate to get past the last false edge.
       * It might be better at some point to derive the arena size.
       *
       * Note also that if we choose at some point in the future to claim
       * regions for executables or libraries, some other mechanism will
       * be needed to skip the main arena structure as a source of edges.
       */
      Offset approximateArenaSize = (_arenaSizeOffset != 0)
                                        ? (_arenaSizeOffset + OFFSET_SIZE)
                                        : (0x10 + 0x10f * OFFSET_SIZE);
      _virtualMemoryPartition.ClaimRange(
          _mainArenaAddress, approximateArenaSize, LIBC_MALLOC_MAIN_ARENA);
    }

    /*
     * If we have reached this point we have found the address of at least
     * one arena, and so it is likely that libc malloc is in use.  Even
     * if the main arena was not actually found it is expected to have been
     * present in the process and it will still be necessary to find any
     * allocations associated with the main arena.
     */

    FindMainArenaPageRuns();

    ScanForLargeChunks();

    FindAllAllocations();

    CorrectFreeListStatus();

    CheckForCorruption();
  }

  virtual ~LibcMallocAllocationFinder() {}
  // returns NumAllocations() if offset is not in any range.

  virtual AllocationIndex AllocationIndexOf(Offset addr) const {
    size_t limit = _allocations.size();
    size_t base = 0;
    while (base < limit) {
      size_t mid = (base + limit) / 2;
      const Allocation& allocation = _allocations[mid];
      if (addr >= allocation.Address()) {
        if (addr < allocation.Address() + allocation.Size()) {
          return (AllocationIndex)(mid);
        } else {
          base = mid + 1;
        }
      } else {
        limit = mid;
      }
    }
    return _allocations.size();
  }

  // null if index is not valid.
  virtual const Allocation* AllocationAt(AllocationIndex index) const {
    if (index < _allocations.size()) {
      return &_allocations[index];
    }
    return (const Allocation*)0;
  }

  virtual AllocationIndex NumAllocations() const { return _allocations.size(); }

  virtual AllocationIndex EdgeTargetIndex(Offset targetCandidate) const {
    // TODO - move as default implementation to base?
    AllocationIndex targetIndex = AllocationIndexOf(targetCandidate);
    if (targetIndex != _allocations.size()) {
      const Allocation* target = AllocationAt(targetIndex);
      if (target != 0) {
        return targetIndex;
      }
    }
    return _allocations.size();
  }

  const char* LIBC_MALLOC_HEAP;
  const char* LIBC_MALLOC_MAIN_ARENA;
  const char* LIBC_MALLOC_MAIN_ARENA_PAGES;
  const char* LIBC_MALLOC_LARGE_ALLOCATIONS;

 private:
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const VirtualAddressMap<Offset>& _addressMap;

  std::vector<Allocation> _allocations;

  static const Offset OFFSET_SIZE = sizeof(Offset);
  static const Offset DEFAULT_MAX_HEAP_SIZE =
      (OFFSET_SIZE == 4) ? 0x100000 : 0x4000000;
  struct Arena {
    Arena(Offset address)
        : _address(address),
          _nextArena(0),
          _top(0),
          _size(0),
          _hasFastBinCorruption(false),
          _hasFreeListCorruption(false) {}
    Offset _address;
    Offset _nextArena;
    Offset _top;
    Offset _size;
    bool _hasFastBinCorruption;
    bool _hasFreeListCorruption;  // ... in doubly linked list
  };
  typedef std::map<Offset, Arena> ArenaMap;
  typedef typename ArenaMap::iterator ArenaMapIterator;
  typedef typename ArenaMap::const_iterator ArenaMapConstIterator;

  struct Heap {
    Heap(Offset address, Offset arenaAddress, Offset size, Offset maxSize,
         Offset nextHeap)
        : _address(address),
          _arenaAddress(arenaAddress),
          _size(size),
          _maxSize(maxSize),
          _nextHeap(nextHeap) {}
    Offset _address;
    Offset _arenaAddress;
    Offset _size;
    Offset _maxSize;
    Offset _nextHeap;
  };
  typedef std::map<Offset, Heap> HeapMap;
  typedef typename HeapMap::iterator HeapMapIterator;
  typedef typename HeapMap::const_iterator HeapMapConstIterator;

  // Keep the start and size for every run of arena pages, in order
  // of start address.
  typedef std::vector<std::pair<Offset, Offset> > MainArenaPages;
  typedef typename MainArenaPages::iterator MainArenaPagesIterator;
  typedef typename MainArenaPages::const_iterator MainArenaPagesConstIterator;

  // Keep the start and size for every memory range containing a large
  // allocation, in order of start address, and including any overhead
  // before or after the allocation.
  typedef std::vector<std::pair<Offset, Offset> > LargeAllocations;
  typedef typename LargeAllocations::iterator LargeAllocationsIterator;
  typedef
      typename LargeAllocations::const_iterator LargeAllocationsConstIterator;

  HeapMap _heaps;
  ArenaMap _arenas;
  MainArenaPages _mainArenaPages;
  LargeAllocations _largeAllocations;
  Offset _mainArenaAddress;
  bool _mainArenaIsContiguous;
  Offset _arenaNextOffset;
  Offset _arenaSizeOffset;
  Offset _arenaTopOffset;
  Offset _arenaDoublyLinkedFreeListOffset;
  Offset _arenaLastDoublyLinkedFreeListOffset;
  Offset _arenaStructSize;
  Offset _maxHeapSize;

  void RecordAllocated(Offset address, Offset size) {
    _allocations.push_back(Allocation(address, size, true));
  }

  void RecordFree(Offset address, Offset size) {
    _allocations.push_back(Allocation(address, size, false));
  }

  bool IsTextAddress(Offset address) {
    // TODO: move to base as default or to VAM?
    typename VirtualAddressMap<Offset>::const_iterator it =
        _addressMap.find(address);
    if (it != _addressMap.end() &&
        (it.Flags() &
         (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE |
          RangeAttributes::IS_EXECUTABLE)) ==
            (RangeAttributes::IS_READABLE | RangeAttributes::IS_EXECUTABLE)) {
      return true;
    }
    return false;
  }

  void FindHeapAndArenaCandidates() {
    typename VirtualAddressMap<Offset>::const_iterator itEnd =
        _addressMap.end();
    for (typename VirtualAddressMap<Offset>::const_iterator it =
             _addressMap.begin();
         it != itEnd; ++it) {
      const char* image = it.GetImage();
      if (image == (const char*)0) {
        continue;
      }
      if ((it.Flags() &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) !=
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
        continue;
      }
      Offset base = it.Base();
      Offset limit = it.Limit();

      for (Offset heapStart = (base + (_maxHeapSize - 1)) & ~(_maxHeapSize - 1);
           heapStart != 0 && heapStart + 0x1000 <= limit;
           heapStart += _maxHeapSize) {
        Offset* headers = ((Offset*)(image + (heapStart - base)));
        Offset arenaAddress = headers[0];
        if ((arenaAddress & (_maxHeapSize - 1)) == (OFFSET_SIZE * 4) &&
            (headers[1] & (_maxHeapSize - 1)) == 0 && headers[2] != 0 &&
            (headers[2] & 0xFFF) == 0 && headers[3] != 0 &&
            (headers[3] & 0xFFF) == 0 &&
            ((headers[0] & ~(_maxHeapSize - 1)) == heapStart) ==
                (headers[1] == 0)) {
          if (arenaAddress == heapStart + (OFFSET_SIZE * 4)) {
            (void)_arenas
                .insert(std::make_pair(arenaAddress, Arena(arenaAddress)))
                .first;
          }
          _heaps.insert(std::make_pair(
              heapStart,
              Heap(heapStart, headers[0], headers[2], headers[3], headers[1])));
        }
      }
    }
  }

  size_t CheckAsTopOffset(Offset candidate) {
    size_t numVotes = 0;
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      try {
        Offset top = reader.ReadOffset(arena._address + candidate);
        Offset topSizeAndFlags = reader.ReadOffset(top + OFFSET_SIZE);
        if (((top + (topSizeAndFlags & ~7)) & 0xFFF) == 0) {
          numVotes++;
        }
      } catch (NotMapped&) {
      }
    }
    return numVotes;
  }

  size_t CheckFreeListOffset(Offset candidate) {
    size_t numVotes = 0;
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      Offset adjustedHeader = arena._address + candidate - OFFSET_SIZE * 2;
      try {
        Offset first = reader.ReadOffset(adjustedHeader + OFFSET_SIZE * 2);
        Offset last = reader.ReadOffset(adjustedHeader + OFFSET_SIZE * 3);
        if ((first == adjustedHeader && last == adjustedHeader) ||
            (reader.ReadOffset(first + OFFSET_SIZE * 3) == adjustedHeader &&
             reader.ReadOffset(last + OFFSET_SIZE * 2) == adjustedHeader)) {
          numVotes++;
        }
      } catch (NotMapped&) {
      }
    }
    return numVotes;
  }

  size_t CheckNextOffset(Offset candidate, Offset& mainArenaCandidate) {
    size_t numVotes = 0;
    mainArenaCandidate = 0;
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      try {
        Offset next = reader.ReadOffset(arena._address + candidate);
        if ((next & (OFFSET_SIZE - 1)) == 0 && next != 0) {
          if (_arenas.find(next) != _arenas.end()) {
            numVotes++;
          } else {
            Offset nextNext = reader.ReadOffset(next + candidate);
            if (_arenas.find(nextNext) != _arenas.end()) {
              numVotes++;
              if ((next & 0xFFFFF) != (OFFSET_SIZE * 4)) {
                mainArenaCandidate = next;
              } else {
                std::cerr << "Arena at " << std::hex << arena._address
                          << " has unexpected next: " << next << std::endl;
              }
            }
          }
        }
      } catch (NotMapped&) {
      }
    }
    return numVotes;
  }

  size_t CheckSizeOffset(Offset candidate) {
    size_t numVotes = 0;
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      try {
        Offset size = reader.ReadOffset(arena._address + candidate);
        if (_arenas.find(size) == _arenas.end() && size != 0 &&
            (size & 0xFFF) == 0) {
          numVotes++;
        }
      } catch (NotMapped&) {
      }
    }
    return numVotes;
  }

  size_t CheckArenaStructSize(Offset candidate) {
    size_t numVotes = 0;
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      Offset possibleAllocationStart = arena._address + candidate;
      try {
        if ((reader.ReadOffset(possibleAllocationStart) == 0)) {
          Offset sizeAndFlags =
              reader.ReadOffset(possibleAllocationStart + OFFSET_SIZE);
          if ((sizeAndFlags & ~(_maxHeapSize - 4)) == 1) {
            numVotes++;
          }
        }
      } catch (NotMapped&) {
      }
    }
    return numVotes;
  }

  Offset RescanForHeapsBasedOnKnownArenas(
      std::vector<Offset>& newlyFoundHeaps) {
    Offset addedHeapSizes = 0;
    typename VirtualAddressMap<Offset>::const_iterator itEnd =
        _addressMap.end();
    for (typename VirtualAddressMap<Offset>::const_iterator it =
             _addressMap.begin();
         it != itEnd; ++it) {
      const char* image = it.GetImage();
      if (image == (const char*)0) {
        continue;
      }
      if ((it.Flags() &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) !=
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
        continue;
      }
      Offset base = it.Base();
      Offset limit = it.Limit();

      for (Offset heapStart = (base + (_maxHeapSize - 1)) & ~(_maxHeapSize - 1);
           heapStart != 0 && heapStart + 0x1000 <= limit;
           heapStart += _maxHeapSize) {
        if (_heaps.find(heapStart) != _heaps.end()) {
          continue;
        }
        Offset* headers = ((Offset*)(image + (heapStart - base)));
        Offset arenaAddress = headers[0];
        if ((arenaAddress & (_maxHeapSize - 1)) == (OFFSET_SIZE * 4) &&
            (headers[1] & (_maxHeapSize - 1)) == 0 && headers[2] != 0 &&
            (headers[2] & 0xFFF) == 0 && headers[3] != 0 &&
            (headers[3] & 0xFFF) == 0 &&
            ((headers[0] & ~(_maxHeapSize - 1)) == heapStart) ==
                (headers[1] == 0) &&
            _arenas.find(arenaAddress) != _arenas.end()) {
          _heaps.insert(std::make_pair(
              heapStart,
              Heap(heapStart, headers[0], headers[2], headers[3], headers[1])));
          addedHeapSizes += headers[2];
          newlyFoundHeaps.push_back(heapStart);
        }
      }
    }

    return addedHeapSizes;
  }

  void SetArenasBasedOnRing(const std::vector<Offset> arenaAddresses) {
    _arenas.clear();
    size_t numArenas = arenaAddresses.size();
    for (size_t i = 0; i < numArenas; i++) {
      Offset arenaAddress = arenaAddresses[i];
      ArenaMapIterator it =
          _arenas.insert(std::make_pair(arenaAddress, Arena(arenaAddress)))
              .first;

      Arena& arena = it->second;
      arena._nextArena = arenaAddresses[(i + 1) % numArenas];
    }

    /*
     * Given that all the arenas have been found it should be safe to
     * derive the offsets of various fields.  This also fills in various
     * fields of the Arena objects, such as the _size field, based on the
     * derived offsets.
     */

    DeriveArenaOffsets();

    /*
     * Calculate the sum of the non-main arena sizes, as for use below in
     * sanity checking that the sum of the sizes of the heaps found basically
     * covers the sum of the sizes associated with every non-main arena.
     */

    Offset sumOfNonMainArenaSizes = 0;
    Offset orOfNonMainArenaFirstHeaps = 0;
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Offset arenaAddress = it->first;
      if (arenaAddress != _mainArenaAddress) {
        sumOfNonMainArenaSizes += it->second._size;
        Offset firstHeapAddress = arenaAddress - 4 * OFFSET_SIZE;
        orOfNonMainArenaFirstHeaps |= firstHeapAddress;
      }
    }

    /*
     * Given that the set of arenas is trusted now we can assume that any
     * heaps that don't refer to one of them must be false.
     */

    Offset maximumRWHeapBytes = 0;
    Offset totalHeapSizes = 0;
    for (HeapMapIterator it = _heaps.begin(); it != _heaps.end();) {
      Offset heapAddress = it->first;
      Heap& heap = it->second;
      Offset arenaAddress = heap._arenaAddress;
      if (_arenas.find(arenaAddress) == _arenas.end()) {
        std::cerr << "Ignoring false heap at " << std::hex << heapAddress
                  << std::endl;
        _heaps.erase(it++);
      } else {
        Offset rwHeapBytes = heap._maxSize;
        if (maximumRWHeapBytes < rwHeapBytes) {
          maximumRWHeapBytes = rwHeapBytes;
        }
        totalHeapSizes += heap._size;
        ++it;
      }
    }

    if (maximumRWHeapBytes > _maxHeapSize) {
      /*
       * This has not been seen but in theory could happen with a
       * glibc compile-time maximum heap size larger than the default.
       * It could also happen with corruption of the heap.  That hasn't
       * been seen either but if this ever happens the code should be
       * made more robust.
       */
      std::cerr << "At least one heap appears to be larger than the default"
                << " maximum, 0x" << std::hex << DEFAULT_MAX_HEAP_SIZE
                << std::endl;
      while (maximumRWHeapBytes > _maxHeapSize) {
        _maxHeapSize = _maxHeapSize << 1;
      }
      std::cerr << "A new maximum heap size of 0x" << std::hex << _maxHeapSize
                << " will be used." << std::endl;
      if (totalHeapSizes < sumOfNonMainArenaSizes) {
        std::cerr << "Some heaps are probably missing." << std::endl;
        std::cerr << "Leak analysis will be inaccurate." << std::endl;
      }
      return;
    }

    Offset minMaxHeapSize = 0x10000;
    if (minMaxHeapSize < maximumRWHeapBytes) {
      minMaxHeapSize = maximumRWHeapBytes;
    }
    while ((orOfNonMainArenaFirstHeaps & (_maxHeapSize - 1)) != 0) {
      if (_maxHeapSize < minMaxHeapSize) {
        std::cerr << "Maximum heap size appears to differ from default "
                  << "but not be valid.\n";
        std::cerr << "Using default.\n";
        _maxHeapSize = DEFAULT_MAX_HEAP_SIZE;
        return;
      }
      _maxHeapSize = _maxHeapSize >> 1;
    }

    if (_maxHeapSize < DEFAULT_MAX_HEAP_SIZE) {
      std::cerr << "Maximum heap size seems to be at most 0x" << std::hex
                << _maxHeapSize << " rather than default 0x"
                << DEFAULT_MAX_HEAP_SIZE << "." << std::endl;
      std::vector<Offset> newlyFoundHeaps;
      Offset numHeapBytesFound =
          RescanForHeapsBasedOnKnownArenas(newlyFoundHeaps);
      if (!newlyFoundHeaps.empty()) {
        totalHeapSizes += numHeapBytesFound;
        std::cerr << "Found " << std::dec << newlyFoundHeaps.size()
                  << " additional heaps at lower max heap size 0x" << std::hex
                  << _maxHeapSize << "." << std::endl;
      }
    }

    if (totalHeapSizes < sumOfNonMainArenaSizes) {
      /*
       * This might happen for 1 of several reasons.  One could be that
       * a heap is actually missing from the core, either due to a bug
       * in gdb for example that might cause it to omit a page that is
       * non-resident or due to gdb being killed after it had allocated
       * the full size of the core but before it filled in all the pages
       * or because the maximum heap size is less than what has been
       * calculated so far, in which case we should try lower maximum
       * heap size values, or because at least one heap/arena pair was
       * under flux, rendering the values temporarily inconsistent.
       */
      Offset lastMaxHeapSizeWithHeap = _maxHeapSize;
      while ((_maxHeapSize >> 1) >= minMaxHeapSize) {
        _maxHeapSize = _maxHeapSize >> 1;
        std::vector<Offset> newlyFoundHeaps;
        Offset numHeapBytesFound =
            RescanForHeapsBasedOnKnownArenas(newlyFoundHeaps);

        if (numHeapBytesFound > 0) {
          std::cerr << "Found " << std::dec << newlyFoundHeaps.size()
                    << "additional heaps at lower max heap size 0x"
                    << _maxHeapSize << "." << std::endl;
          lastMaxHeapSizeWithHeap = _maxHeapSize;
          totalHeapSizes += numHeapBytesFound;
          if (totalHeapSizes >= sumOfNonMainArenaSizes) {
            return;
          }
        }
      }
      _maxHeapSize = lastMaxHeapSizeWithHeap;
      std::cerr << "The sum of the heap sizes, 0x" << std::hex << totalHeapSizes
                << ", is less than the sum of" << std::endl
                << "the non-main arena sizes, 0x" << sumOfNonMainArenaSizes
                << "." << std::endl;
      std::cerr << "Some heaps may be missing." << std::endl
                << "Leak analysis will be inaccurate." << std::endl;
    }
  }

  /*
   * This is useful in the case that no non-main arenas have been found in
   * the scan by heaps, but the main arena has, if we need to rule out the
   * uncommon case that glibc has been compiled in such a way that the
   * constant for the maximum heap size differs from the standard one.
   * Return true if this finds at least one non-main arena, or false
   * otherwise.
   */

  bool FindNonMainArenasByRingFromMainArena() {
    Reader reader(_addressMap);
    try {
      Offset limit = _mainArenaAddress + 0x120 * OFFSET_SIZE;
      for (Offset checkAt = _mainArenaAddress + 0x80 * OFFSET_SIZE;
           checkAt < limit; checkAt += OFFSET_SIZE) {
        if (reader.ReadOffset(checkAt) == _mainArenaAddress) {
          /*
           * The arena points to itself so there really is just one
           * arena and no non-main arenas exist.
           */
          return false;
        }
      }

      for (Offset checkAt = _mainArenaAddress; checkAt < limit;
           checkAt += OFFSET_SIZE) {
        Offset candidate = reader.ReadOffset(checkAt);
        Offset nextOffset = checkAt - _mainArenaAddress;
        std::vector<Offset> candidates;

        while ((candidate & 0xffff) == (4 * OFFSET_SIZE)) {
          candidates.push_back(candidate);
          candidate = reader.ReadOffset(candidate + nextOffset);
        }
        if (candidate == _mainArenaAddress) {
          /*
           * We had to have made it at least one time through the ring
           * because the value of candidate before the loop was known
           * not to be _mainArenaAddress.
           */
          candidates.push_back(_mainArenaAddress);
          SetArenasBasedOnRing(candidates);
          return true;
        }
      }
    } catch (NotMapped&) {
    }
    return false;
  }

  bool FindArenasByRingFromNonMainArenas() {
    OffsetSet notInCompletedRing;
    Offset bestMainArenaCandidate = 0;
    size_t bestNumVotes = 0;
    Offset bestNextOffset = 0;
    size_t numArenas = _arenas.size();
    for (Offset candidateOffset = 0x60 * OFFSET_SIZE;
         candidateOffset < 0x120 * OFFSET_SIZE;
         candidateOffset += OFFSET_SIZE) {
      Offset mainArenaCandidate = 0;
      size_t numVotes = CheckNextOffset(candidateOffset, mainArenaCandidate);
      if (mainArenaCandidate == 0) {
        continue;
      }
      if (bestNumVotes < numVotes) {
        bestNumVotes = numVotes;
        bestMainArenaCandidate = mainArenaCandidate;
        bestNextOffset = candidateOffset;
        if (bestNumVotes == numArenas) {
          break;
        }
      }
    }
    if (bestMainArenaCandidate == 0) {
      return false;
    }
    _mainArenaAddress = bestMainArenaCandidate;
    (void) _arenas.insert(std::make_pair(_mainArenaAddress, Arena(_mainArenaAddress)))
            .first;

    Offset arenaAddress = _mainArenaAddress;
    std::vector<Offset> inRing;
    Reader reader(_addressMap);
    try {
      do {
        Offset nextArena = reader.ReadOffset(arenaAddress + bestNextOffset);
        inRing.push_back(arenaAddress);
        arenaAddress = nextArena;
        if (arenaAddress == _mainArenaAddress) {
          SetArenasBasedOnRing(inRing);
          return true;  // The ring was found.
        }
      } while ((arenaAddress & 0xffff) == (4 * OFFSET_SIZE));
    } catch (NotMapped&) {
    }
    return false;  // The ring was never found.
  }

  void DeriveArenaOffsets() {
    size_t numArenas = _arenas.size();
    _arenaTopOffset = 0xb * OFFSET_SIZE;
    size_t newTopVotes = CheckAsTopOffset(_arenaTopOffset);
    if (newTopVotes != numArenas) {
      size_t numBadTops = numArenas - newTopVotes;
      size_t oldTopVotes = CheckAsTopOffset(0xc * OFFSET_SIZE);
      if (oldTopVotes > newTopVotes) {
        _arenaTopOffset = 0xc * OFFSET_SIZE;
        numBadTops = numArenas - oldTopVotes;
      }
      if (numBadTops > 0) {
        std::cerr << std::dec << numBadTops
                  << " arenas have unexpected top values." << std::endl;
        if (numBadTops == numArenas) {
          std::cerr << "Possibly the version of libc is not yet supported."
                    << std::endl;
        }
      }
    }
    size_t numListOffsetVotes = 0;
    for (Offset freeListOffset = _arenaTopOffset + OFFSET_SIZE;
         freeListOffset < 0x100; freeListOffset += OFFSET_SIZE) {
      numListOffsetVotes = CheckFreeListOffset(freeListOffset);
      if (numListOffsetVotes > 0) {
        _arenaDoublyLinkedFreeListOffset = freeListOffset;
        break;
      }
    }
    if (numListOffsetVotes < numArenas) {
      if (numListOffsetVotes == 0) {
        std::cerr << "The arena format is totally unrecognized.\n";
        abort();
      } else {
        std::cerr << "At least one arena has an invalid doubly linked list"
                  << " at offset 0x" << std::hex
                  << _arenaDoublyLinkedFreeListOffset;
      }
    }
    for (Offset freeListOffset =
             _arenaDoublyLinkedFreeListOffset + 2 * OFFSET_SIZE;
         freeListOffset < 0x130 * OFFSET_SIZE;
         freeListOffset += 2 * OFFSET_SIZE) {
      numListOffsetVotes = CheckFreeListOffset(freeListOffset);
      if (numListOffsetVotes == 0) {
        break;
      }
      _arenaLastDoublyLinkedFreeListOffset = freeListOffset;
    }
    size_t bestNextOffsetVotes = 0;

    for (Offset nextOffset = _arenaLastDoublyLinkedFreeListOffset +
            2 * OFFSET_SIZE;
         nextOffset < 0x130 * OFFSET_SIZE; nextOffset += OFFSET_SIZE) {
      Offset mainArenaCandidate;
      size_t numVotes = CheckNextOffset(nextOffset, mainArenaCandidate);
      if (bestNextOffsetVotes < numVotes) {
        bestNextOffsetVotes = numVotes;
        _arenaNextOffset = nextOffset;
        if (mainArenaCandidate != 0) {
          _mainArenaAddress = mainArenaCandidate;
        }
        if (numVotes == numArenas) {
          break;
        }
      }
    }
    if (bestNextOffsetVotes < numArenas) {
      if (bestNextOffsetVotes == 0) {
        std::cerr << "The arena next pointer was not found.\n";
        abort();
      } else {
        std::cerr << "At least one arena has an invalid next pointer"
                  << " at offset 0x" << std::hex << _arenaNextOffset
                  << std::endl;
      }
    }

    size_t bestSizeOffsetVotes = 0;
    for (Offset sizeOffset = _arenaNextOffset + OFFSET_SIZE;
         sizeOffset < _arenaNextOffset + OFFSET_SIZE * 8;
         sizeOffset += OFFSET_SIZE) {
      size_t numVotes = CheckSizeOffset(sizeOffset);
      if (bestSizeOffsetVotes < numVotes) {
        bestSizeOffsetVotes = numVotes;
        _arenaSizeOffset = sizeOffset;
        if (numVotes == numArenas) {
          break;
        }
      }
    }
    if (bestSizeOffsetVotes < numArenas) {
      if (bestSizeOffsetVotes == 0) {
        std::cerr << "The arena size field was not found.\n";
        abort();
      } else {
        std::cerr << "At least one arena has an invalid arena size field"
                  << " at offset 0x" << std::hex << _arenaSizeOffset
                  << std::endl;
      }
    }

    size_t numNonMainArenas = _arenas.size();
    if (_mainArenaAddress != 0) {
      numNonMainArenas--;
    }
    _arenaStructSize =
        ((_arenaSizeOffset + 2 * OFFSET_SIZE) & ~((2 * OFFSET_SIZE - 1)));
    if (numNonMainArenas > 0) {
      size_t bestArenaStructSizeVotes = 0;
      for (Offset arenaStructSize = _arenaStructSize;
           arenaStructSize < _arenaStructSize + OFFSET_SIZE * 10;
           arenaStructSize += OFFSET_SIZE) {
        size_t numVotes = CheckArenaStructSize(arenaStructSize);
        if (bestArenaStructSizeVotes < numVotes) {
          bestArenaStructSizeVotes = numVotes;
          _arenaStructSize = arenaStructSize;
          if (numVotes == numNonMainArenas) {
            break;
          }
        }
      }
      if (bestArenaStructSizeVotes < numNonMainArenas) {
        if (bestArenaStructSizeVotes == 0) {
          std::cerr << "The arena structure size was not derived.\n";
          abort();
        } else {
          std::cerr << "At least one arena has an invalid heap start.\n";
        }
      }
    }

    typename VirtualAddressMap<Offset>::Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      Offset arenaAddress = arena._address;
      try {
        arena._top = reader.ReadOffset(arenaAddress + _arenaTopOffset);
        arena._nextArena = reader.ReadOffset(arenaAddress + _arenaNextOffset);
        arena._size = reader.ReadOffset(arenaAddress + _arenaSizeOffset);
      } catch (NotMapped&) {
        std::cerr << "Arena at " << std::hex << arenaAddress
                  << " is not fully mapped.\n";
      }
    }
  }

  void CheckHeapArenaReferences() {
    /*
     * Consider any heap that doesn't refer to an arena in a heap to be a
     * false heap.
     */
    for (HeapMapIterator it = _heaps.begin(); it != _heaps.end();) {
      Offset heapAddress = it->first;
      Heap& heap = it->second;
      Offset arenaAddress = heap._arenaAddress;
      Offset arenaHeapAddress = arenaAddress & ~(_maxHeapSize - 1);
      if (arenaHeapAddress != heapAddress &&
          _arenas.find(arenaAddress) == _arenas.end()) {
        /*
         * The heap refers to an arena that was not detected as being
         * associated with any other heap.  There are recent versions
         * of gdb that can omit pages in certain situations.  To help with
         * the guess about whether the heap is false or not we attempt
         * to check whether there appears to be a reasonable start of
         * a run in the heap.
         */
        Reader reader(_addressMap);
        try {
          Offset chunkAddr = heapAddress + OFFSET_SIZE * 5;
          Offset sizeAndFlags = reader.ReadOffset(chunkAddr);
          Offset mask = ~0x80000 | 2;
          int numSizesOk = 0;
          while ((sizeAndFlags & mask) == 0 && ++numSizesOk < 10) {
            chunkAddr += (sizeAndFlags & (Offset)(~7));
            if (chunkAddr > heapAddress + _maxHeapSize) {
              break;
            }
            sizeAndFlags = reader.ReadOffset(chunkAddr);
          }
          if (numSizesOk == 10 || chunkAddr > heapAddress + _maxHeapSize) {
            _arenas.insert(std::make_pair(arenaAddress, Arena(arenaAddress)));
            std::cerr << "Arena at " << std::hex << arenaAddress
                      << " appears to be "
                      << ((_addressMap.find(arenaAddress) == _addressMap.end())
                              ? "missing from the core."
                              : "corrupt.")
                      << std::endl
                      << "Leak analysis will not be reliable." << std::endl;
            ++it;
            continue;
          }
        } catch (NotMapped&) {
        }
        std::cerr << "Ignoring false heap at " << std::hex << heapAddress
                  << std::endl;
        _heaps.erase(it++);
      } else {
        ++it;
      }
    }
  }

  void CheckArenaTops() {
    /*
     * Scan just the heap-based arenas, expecting every top value to reside
     * within one of the allocated heaps. For now, discard any heaps where
     * this doesn't match but it would be better to allow calculations to
     * continue if not.
     */

    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end();) {
      Arena& arena = it->second;
      if (arena._address == _mainArenaAddress) {
        ++it;
        continue;
      }
      HeapMapIterator topHeap = _heaps.find(arena._top & ~(_maxHeapSize - 1));
      if (topHeap == _heaps.end()) {
        std::cerr << "Arena with invalid top found at " << std::hex
                  << arena._address << std::endl;
        // TODO: deal with possible corruption here
        _heaps.erase(arena._address & ~(_maxHeapSize - 1));
        _arenas.erase(it++);
        continue;
      } else {
        ++it;
      }
    }
  }

  void CheckArenaNexts() {
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      Offset nextArena = arena._nextArena;
      if (_arenas.find(nextArena) == _arenas.end()) {
        /*
         * Regrettably, with some recent versions of gdb that produce
         * incomplete cores, possibly due to mishandling of swapping and
         * sometimes because the user can specifically configure things
         * to allow incomplete cores, the ring can be broken by an
         * incomplete core and we don't know whether the next field was
         * valid or not.
         */
        std::cerr << std::hex << "Arena at 0x" << arena._address
                  << " has questionable next pointer 0x" << nextArena
                  << std::endl;
        std::cerr << "The core may be incomplete and leak analysis "
                  << " is compromised" << std::endl;
      }
      // TODO: clean up handling of incomplete cores.
    }
  }

  bool IsEmptyDoubleFreeList(Reader& reader, Offset listAddr) {
    try {
      return reader.ReadOffset(listAddr + 2 * OFFSET_SIZE) == listAddr &&
             reader.ReadOffset(listAddr + 3 * OFFSET_SIZE) == listAddr;
    } catch (NotMapped&) {
      return false;
    }
  }

  bool IsNonEmptyDoubleFreeList(Reader& reader, Offset listAddr) {
    Reader freeReader(_addressMap);
    try {
      Offset firstFree = reader.ReadOffset(listAddr + 2 * OFFSET_SIZE);
      Offset lastFree = reader.ReadOffset(listAddr + 3 * OFFSET_SIZE);
      if (firstFree != listAddr && lastFree != listAddr &&
          freeReader.ReadOffset(firstFree + 3 * OFFSET_SIZE) == listAddr &&
          freeReader.ReadOffset(lastFree + 2 * OFFSET_SIZE) == listAddr) {
        return true;
      }
    } catch (NotMapped&) {
    }
    return false;
  }
  bool HasPlausibleTop(Reader& reader, Offset candidateTopField) {
    try {
      Offset top = reader.ReadOffset(candidateTopField);
      Offset topSizeAndFlags = reader.ReadOffset(top + OFFSET_SIZE);
      Offset topSize = topSizeAndFlags & ~7;
      return ((top + topSize) & 0xfff) == 0;
    } catch (NotMapped&) {
    }
    return false;
  }

  bool ScanForMainArenaByEmptyFreeLists(Offset base, Offset limit) {
    Offset mainArenaCandidate = 0;
    Offset minListAddr = base + 13 * OFFSET_SIZE;
    Offset maxListAddr = limit - 4 * OFFSET_SIZE;
    Reader reader(_addressMap);
    for (Offset listAddr = minListAddr; listAddr < maxListAddr;) {
      if (!IsEmptyDoubleFreeList(reader, listAddr)) {
        listAddr += OFFSET_SIZE;
        continue;
      }
      Offset checkNonEmpty = listAddr - 2 * OFFSET_SIZE;
      while (checkNonEmpty >= minListAddr &&
             IsNonEmptyDoubleFreeList(reader, checkNonEmpty)) {
        checkNonEmpty -= 2 * OFFSET_SIZE;
      }
      Offset runBase = checkNonEmpty + 2 * OFFSET_SIZE;
      Offset runLimit = listAddr + 2 * OFFSET_SIZE;
      while (runLimit <= maxListAddr &&
             (IsEmptyDoubleFreeList(reader, runLimit) ||
              IsNonEmptyDoubleFreeList(reader, runLimit))) {
        runLimit += 2 * OFFSET_SIZE;
      }

      bool extendedBefore = false;
      bool extendedAfter = false;
      if ((runLimit - runBase) < 120 * 2 * OFFSET_SIZE) {
        Offset checkBefore = runBase - 4 * OFFSET_SIZE;
        /*
         * Tolerate a single chain not making sense because the arena
         * may be under flux.  Check if the previous chain was under
         * flux.
         * Note that we don't need to check the empty free list case
         * because we would have caught the skip going forward
         * from that run.
         */
        while ((checkBefore >= minListAddr) &&
               IsNonEmptyDoubleFreeList(reader, checkBefore)) {
          extendedBefore = true;
          checkBefore -= 2 * OFFSET_SIZE;
        }
        if (extendedBefore) {
          /*
           * It is likely the the previous list was under
           * flux, given the rarity of the empty double free list case
           * and the format of the arena.
           */
          runBase = checkBefore + 2 * OFFSET_SIZE;
        } else {
          /*
           * Check if the following chain was under flux.
           */
          Offset checkAfter = runLimit + 2 * OFFSET_SIZE;
          while (checkAfter <= maxListAddr &&
                 (IsEmptyDoubleFreeList(reader, checkAfter) ||
                  IsNonEmptyDoubleFreeList(reader, runLimit))) {
            extendedAfter = true;
            checkAfter += 2 * OFFSET_SIZE;
          }
          if (extendedAfter) {
            runLimit = checkAfter;
          }
        }
      }
      if ((runLimit - runBase) >= 120 * 2 * OFFSET_SIZE) {
        if (HasPlausibleTop(reader, runBase)) {
          /*
           * This is the normal case, when the arena is not under
           * flux  and under recent versions.
           */
          mainArenaCandidate = runBase - 10 * OFFSET_SIZE - 2 * sizeof(int);
          break;
        } else if (!extendedBefore && !extendedAfter &&
                   HasPlausibleTop(reader, runBase - 2 * OFFSET_SIZE)) {
          /*
           * This may happen if the doubly linked list of variable
           * sized chunks was under flux at the time of the core.
           */
          mainArenaCandidate = runBase - 12 * OFFSET_SIZE - 2 * sizeof(int);
          break;
        }
      }
      listAddr = runLimit;
    }
    if (mainArenaCandidate != 0) {
      /*
       * This is necessary because the maximum heap size may differ from
       * the default maximum heap size.  We don't want to treat a missed
       * non-main arena as the main arena.
       */
      bool isNonMainArena = false;
      Offset heapCandidate = mainArenaCandidate - 4 * OFFSET_SIZE;
      if ((heapCandidate & 0xffff) == 0) {
        try {
          if (mainArenaCandidate == reader.ReadOffset(heapCandidate)) {
            isNonMainArena = true;
          }
        } catch (NotMapped&) {
        }
      }
      if (!isNonMainArena) {
        _mainArenaAddress = mainArenaCandidate;
      }
    }

    if (_mainArenaAddress) {
      ArenaMapIterator it =
          _arenas
              .insert(
                  std::make_pair(_mainArenaAddress, Arena(_mainArenaAddress)))
              .first;
      Arena& mainArena = it->second;
      mainArena._nextArena = _mainArenaAddress;

      mainArena._top = reader.ReadOffset(_mainArenaAddress + 12 * OFFSET_SIZE);
      mainArena._size =
          reader.ReadOffset(_mainArenaAddress + 0x10 + 0x10e * OFFSET_SIZE);
      _mainArenaIsContiguous =
          (reader.ReadU32(_mainArenaAddress + sizeof(int)) & 2) == 0;
      return true;
    }
    return false;
  }

  void ScanForTops(Offset base, Offset limit, OffsetSet& topCandidates) {
    Reader reader(_addressMap);
    for (Offset check = base; check < limit; check += (2 * OFFSET_SIZE)) {
      Offset topSize = reader.ReadOffset(check + OFFSET_SIZE) & ~7;
      if (topSize >= 2 * OFFSET_SIZE && topSize <= _maxHeapSize &&
          ((check + topSize) & 0xFFF) == 0) {
        topCandidates.insert(check);
      }
    }
  }

  bool ScanForMainArena(Offset base, Offset limit, OffsetSet& topCandidates) {
    size_t scanWindow = 0;
    Offset lastArenaCandidate = 0;
    Offset lastArenaNextCandidate = 0;
    Offset lastTopCandidate = 0;
    std::map<Offset, Offset> topReferences;

    Reader reader(_addressMap);
    for (Offset check = base; check < limit; check += (2 * OFFSET_SIZE)) {
      Offset atCheck = reader.ReadOffset(check);

      if (topCandidates.find(atCheck) != topCandidates.end()) {
        topReferences.insert(std::make_pair(check, atCheck));
        scanWindow = 0x120 * OFFSET_SIZE;
      }

      if (scanWindow > 0) {
        if (atCheck + 0x100 * OFFSET_SIZE <= check &&
            atCheck + 0x120 * OFFSET_SIZE >= check) {
          typename std::map<Offset, Offset>::iterator it =
              topReferences.find(atCheck + 12 * OFFSET_SIZE);
          if (it == topReferences.end()) {
            it = topReferences.find(atCheck + 11 * OFFSET_SIZE);
          }
          if (it != topReferences.end()) {
            lastArenaCandidate = atCheck;
            lastArenaNextCandidate = check;
            lastTopCandidate = it->second;
          }
        }

        if (atCheck >= 0x4000 && (atCheck & 0xFFF) == 0 &&
            lastArenaNextCandidate + 2 * OFFSET_SIZE >= check) {
          _mainArenaAddress = lastArenaCandidate;
          ArenaMapIterator it =
              _arenas
                  .insert(std::make_pair(_mainArenaAddress,
                                         Arena(_mainArenaAddress)))
                  .first;
          Arena& mainArena = it->second;
          mainArena._nextArena = _mainArenaAddress;
          mainArena._top = lastTopCandidate;
          mainArena._size = atCheck;

          _mainArenaIsContiguous =
              (reader.ReadU32(_mainArenaAddress + sizeof(int)) & 2) == 0;
          return true;
        }
        scanWindow -= OFFSET_SIZE;
      }
    }
    return false;
  }

  bool ScanForMainArena() {
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator
        itEnd = _virtualMemoryPartition.EndUnclaimedImages();
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator it =
        _virtualMemoryPartition.BeginUnclaimedImages();

    OffsetSet topCandidates;
    for (; it != itEnd; ++it) {
      if ((it->_value &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) ==
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
        ScanForTops(it->_base, it->_limit, topCandidates);
      }
    }
    for (it = _virtualMemoryPartition.BeginUnclaimedImages(); it != itEnd;
         ++it) {
      if ((it->_value &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) ==
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
      }
      if (ScanForMainArena(it->_base, it->_limit, topCandidates)) {
        return true;
      }
    }
    for (it = _virtualMemoryPartition.BeginUnclaimedImages(); it != itEnd;
         ++it) {
      if ((it->_value &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) ==
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
      }
      if (ScanForMainArenaByEmptyFreeLists(it->_base, it->_limit)) {
        return true;
      }
    }
    return false;
  }

  struct RunCandidate {
    RunCandidate()
        : _start(0), _lastCheck(0), _numAllocations(0), _lastPageBoundary(0) {}
    Offset _start;
    Offset _lastCheck;
    Offset _numAllocations;
    Offset _lastPageBoundary;
  };
  typedef std::vector<RunCandidate> RunCandidates;

  void EvaluatePageRunCandidate(Offset base, Offset limit,
                                RunCandidates& candidates) {
    Reader reader(_addressMap);
    if (reader.ReadOffset(base) != 0) {
      return;
    }
    Offset sizeAndFlags = reader.ReadOffset(base + OFFSET_SIZE);
    /*
     * Note that what is valid for the first entry on a run of pages
     * for the main arena is a subset of what is valid for an arbitrary
     * allocation.  That first value must be marked as for the main
     * arena, not be marked as a large chunk, and have a size that
     * corresponsnds to a multiple of 2 times the size of a pointer.
     * In the case of a 4-byte offset that last check becomes irrelevant
     * because it can't fail given that the low 3 bits are for flags.
     * Note that checking bit 2 in that way would not be valid without
     * the assumption hear that we only want bits from the main arena.
     */

    if ((sizeAndFlags & (OFFSET_SIZE | 7)) != 1) {
      return;
    }

    Offset chunkSize = sizeAndFlags & ~7;

    if ((chunkSize == 0) || (chunkSize >= 0x10000000) ||
        (chunkSize > (limit - base))) {
      return;
    }

    size_t numAllocations = 1;
    Offset lastPageBoundary = base;
    Offset check = base + chunkSize;
    while (1) {
      if ((check & 0xfff) == 0) {
        for (typename RunCandidates::const_reverse_iterator it =
                 candidates.rbegin();
             it != candidates.rend(); ++it) {
          /*
           * Note that the size of candidates tends to be really tiny
           * (normally single digits).
           */
          RunCandidate& candidate = candidates.back();
          if (candidate._start == lastPageBoundary) {
            candidate._start = base;
            return;
          }
        }
        lastPageBoundary = check;
      }
      if (check == limit) {
        // We don't need to add OFFSET_SIZE to check because an invariant
        // here is that both are divisible by 2 * OFFSET_SIZE.
        break;
      }
      Offset sizeAndFlags = reader.ReadOffset(check + OFFSET_SIZE);
      if ((sizeAndFlags & (OFFSET_SIZE | 6)) != 0) {
        break;
      }

      Offset chunkSize = sizeAndFlags & ~7;

      if ((chunkSize == 0) || (chunkSize >= 0x10000000) ||
          (chunkSize > (limit - check))) {
        break;
      }

      numAllocations++;
      check += chunkSize;
    }

    if (numAllocations >= 20 || lastPageBoundary > base) {
      candidates.push_back(RunCandidate());
      RunCandidate& candidate = candidates.back();
      candidate._start = base;
      candidate._lastCheck = check;
      candidate._numAllocations = numAllocations;
      candidate._lastPageBoundary = lastPageBoundary;
    }
  }

  void ScanForMainArenaPageRunsInRange(Offset base, Offset limit,
                                       RunCandidates& candidates) {
    limit = limit & ~0xfff;
    base = (base + 0xfff) & ~0xfff;
    RunCandidates candidatesInRange;
    Reader reader(_addressMap);
    for (Offset check = limit - 0x1000; check >= base; check -= 0x1000) {
      EvaluatePageRunCandidate(check, limit, candidatesInRange);
    }
    for (typename RunCandidates::const_reverse_iterator it =
             candidatesInRange.rbegin();
         it != candidatesInRange.rend(); ++it) {
      candidates.push_back(*it);
    }
  }

  void ScanForMainArenaPageRuns(Offset mainArenaSize) {
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator
        itEnd = _virtualMemoryPartition.EndUnclaimedImages();
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator it =
        _virtualMemoryPartition.BeginUnclaimedImages();

    RunCandidates runCandidates;
    for (; it != itEnd; ++it) {
      if ((it->_value &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) ==
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
        ScanForMainArenaPageRunsInRange(it->_base, it->_limit, runCandidates);
      }
    }

    /*
     * Select the _mainArenaPages from the run candidates.
     */

    size_t numRunCandidates = runCandidates.size();
    if (numRunCandidates == 0) {
      std::cerr << "No main arena runs were found.\n";
      std::cerr << "Perhaps libc malloc was not used.\n";
      return;
    }

    if (numRunCandidates == 1) {
      std::cerr << "Probably there was a corrupt single main arena run.\n"
                << "Leak analysis probably will not be correct.\n";
      Offset base = runCandidates[0]._start;
      Offset size = runCandidates[0]._lastPageBoundary - base;
      if (size > mainArenaSize) {
        size = mainArenaSize;
        // TODO, do this more precisely, taking into account the top
        // value.
      }
      _mainArenaPages.push_back(std::make_pair(base, size));

      if (!_virtualMemoryPartition.ClaimRange(base, size,
                                              LIBC_MALLOC_MAIN_ARENA_PAGES)) {
        abort();
      }
      return;
    }

    /*
     * This is a really crude way just picking the first set of candidates
     * that don't overlap, until the size is reached or exceeded.  This
     * should be made more general, because, for example, the first range
     * seen among a set of overlapping ranges, although it is normally
     * the largest, is not necessarily the best.
     *
     * This algorithm should really take the top chunk into account and
     * have logic to pick what to exclude in case too much was found.
     */

    Offset prevLimit = 0;
    Offset totalMainArenaRunSizes = 0;
    for (typename RunCandidates::iterator it = runCandidates.begin();
         it != runCandidates.end(); ++it) {
      Offset base = it->_start;
      Offset size = runCandidates[0]._lastPageBoundary - base;
      if (base < prevLimit) {
        continue;
      }
      if (!_virtualMemoryPartition.ClaimRange(base, size,
                                              LIBC_MALLOC_MAIN_ARENA_PAGES)) {
        abort();
      }
      _mainArenaPages.push_back(std::make_pair(base, size));
      totalMainArenaRunSizes += size;
      prevLimit = base + size;
    }
    if (totalMainArenaRunSizes < mainArenaSize) {
      std::cerr << "Expected total main arena areas of 0x" << std::hex
                << mainArenaSize << " but found 0x" << totalMainArenaRunSizes
                << std::endl
                << "Leak analysis may be inaccurate due to missing chunks."
                << std::endl;
    }
  }

  bool FindSingleContiguousMainArenaRun(Arena& mainArena) {
    Offset top = mainArena._top;

    if (_heaps.find(top & ~(_maxHeapSize - 1)) != _heaps.end()) {
      std::cerr << "Main arena top value, " << std::hex << top
                << ", is in the middle of a heap." << std::endl;
      return false;
    }

    Reader reader(_addressMap);
    Offset topSize = 0;
    try {
      topSize = reader.ReadOffset(top + OFFSET_SIZE);
    } catch (NotMapped&) {
      std::cerr << "The main arena has a top value of " << std::hex << top
                << " which lacks an image in the core.\n";
      return false;
    }

    Offset topLimit = top + (topSize & ~7);
    if ((topSize & 6) != 0) {
      std::cerr << "Main arena top chunk at " << std::hex << top
                << " has corrupt size and flags value " << topSize << std::endl;
      return false;
    }

    if ((topLimit & 0xFFF) != 0) {
      std::cerr << "Main arena top chunk at " << std::hex << top
                << " has corrupt size value " << topSize;
      return false;
    }

    Offset base = topLimit - mainArena._size;

    typename VirtualAddressMap<Offset>::const_iterator itAddressMap =
        _addressMap.find(top);
    if ((itAddressMap == _addressMap.end()) || (base < itAddressMap.Base()) ||
        (topLimit > itAddressMap.Limit())) {
      if (!_mainArenaIsContiguous) {
        /*
         * We didn't have any guarantee from the arena header that the
         * arena was supposed to be contiguous.  So no error is
         * warranted if it is not contiguous.
         */
        return false;
      }

      /*
       * It is still possible to proceed but this mention gives a clue
       * that either the core is incomplete or the arena pages are not
       * fully mapped.
       */

      std::cerr << "Warning: The main arena is expected to be contiguous"
                << " but is not fully mapped.\n";
      if (itAddressMap == _addressMap.end()) {
        std::cerr << "The top area, at 0x" << std::hex << top
                  << " is not mapped at all in the core, suggesting"
                  << " an incompete core.\n";
      } else {
        Offset oldBase = base;
        Offset oldTopLimit = topLimit;
        if (base < itAddressMap.Base()) {
          base = itAddressMap.Base();
        }
        if (topLimit > itAddressMap.Limit()) {
          topLimit = itAddressMap.Limit();
        }
        std::cerr << "A range of [" << std::hex << oldBase << ", "
                  << oldTopLimit << ") was expected.\n"
                                    "Only ["
                  << base << ", " << topLimit << ") was available.\n";
      }

    } else {
      RunCandidates runCandidates;
      EvaluatePageRunCandidate(base, topLimit, runCandidates);
      if (runCandidates.empty()) {
        if (!_mainArenaIsContiguous) {
          /*
           * Given that the start of the range that one would expect
           * if the whole run were contiguous does not look correct,
           * and that we didn't expect it to be contiguous, just
           * stop trying to treat it as contiguous.
           */
          return false;
        }
        /*
         * For now, since it was marked as contiguous we'll go ahead
         * and mark it as such, for purposes of understanding big ranges
         * in the address space.  However, leak analysis will fail and
         * some recovery will be needed to find the allocations that
         * appear after the corruption.
         */
        std::cerr << "Warning: a contiguous range of main arena pages "
                  << "was expected at 0x" << std::hex << base << "\n"
                  << "The start of that range may be corrupted.\n";

      } else if ((runCandidates[0]._lastPageBoundary -
                  runCandidates[0]._start) != mainArena._size) {
        if (!_mainArenaIsContiguous) {
          /*
           * Given that part of the range that one would expect
           * if the whole run were contiguous does not look correct,
           * and that we didn't expect it to be contiguous, just
           * stop trying to treat it as contiguous.
           */
          return false;
        }
        std::cerr << "Warning: a contiguous range of main arena pages "
                  << "was expected at 0x" << std::hex << base << "\n"
                  << "Part of that range is probably corrupted.\n";
      }
    }

    if (!_virtualMemoryPartition.ClaimRange(base, mainArena._size,
                                            LIBC_MALLOC_MAIN_ARENA_PAGES)) {
      std::cerr << "The region " << std::hex << "[0x" << base << ", "
                << topLimit << "] may be inaccurate for main arena pages.\n";
      return false;
    }
    _mainArenaPages.push_back(std::make_pair(base, topLimit - base));
    return true;
  }

  void FindMainArenaPageRuns() {
    Offset mainArenaSize = 0;
    if (_mainArenaAddress != 0) {
      /*
       * The main arena was found.  This is the normal case.
       */

      ArenaMapIterator itArena = _arenas.find(_mainArenaAddress);
      if (itArena == _arenas.end()) {
        /*
         * It is a serious bug in the program if the main arena address
         * was found but no entry was added for that arena.
         */
        abort();
      }
      Arena& mainArena = itArena->second;
      if ((mainArena._size & 0xFFF) == 0) {
        mainArenaSize = mainArena._size;
        if (FindSingleContiguousMainArenaRun(mainArena)) {
          return;
        }
      } else {
        std::cerr << "The arena size value, 0x" << std::hex << mainArena._size
                  << " appears to be corrupt.\n";
      }
    }
    ScanForMainArenaPageRuns(mainArenaSize);
  }

  void ScanForLargeChunksInRange(Offset base, Offset limit) {
    Reader reader(_addressMap);
    Offset candidate = (base + 0xFFF) & ~0xFFF;
    while (candidate <= limit - 0x1000) {
      Offset expect0 = reader.ReadOffset(candidate);
      Offset chunkSizeAndFlags = reader.ReadOffset(candidate + OFFSET_SIZE);
      bool foundLargeAlloc =
          (expect0 == 0) &&
          ((chunkSizeAndFlags & ((Offset)0xFFF)) == ((Offset)2)) &&
          (chunkSizeAndFlags >= ((Offset)0x1000)) &&
          (candidate + chunkSizeAndFlags - 2) <= limit;
      if (!foundLargeAlloc) {
        candidate += 0x1000;
      } else {
        Offset chunkSize = chunkSizeAndFlags - 2;
        _largeAllocations.push_back(std::make_pair(candidate, chunkSize));
        candidate += chunkSize;
      }
    }
  }

  void ScanForLargeChunks() {
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator
        itEnd = _virtualMemoryPartition.EndUnclaimedImages();
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator it =
        _virtualMemoryPartition.BeginUnclaimedImages();

    for (; it != itEnd; ++it) {
      if ((it->_value &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) ==
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
        ScanForLargeChunksInRange(it->_base, it->_limit);
      }
    }
    for (LargeAllocationsConstIterator itLarge = _largeAllocations.begin();
         itLarge != _largeAllocations.end(); ++itLarge) {
      _virtualMemoryPartition.ClaimRange(itLarge->first, itLarge->second,
                                         LIBC_MALLOC_LARGE_ALLOCATIONS);
    }
  }

  void AddLargeAllocation(Offset start, Offset size) {
    RecordAllocated(start + 2 * OFFSET_SIZE, size - 2 * OFFSET_SIZE);
  }

  Offset SkipArenaCorruption(Offset arenaAddress, Offset corruptionPoint,
                             Offset repairLimit) {
    Offset pastArenaCorruption = 0;
    repairLimit -= 6 * OFFSET_SIZE;

    Offset expectClearMask = 2;
    if (arenaAddress == _mainArenaAddress) {
      expectClearMask = expectClearMask | 4;
    }
    if (OFFSET_SIZE == 8) {
      expectClearMask = expectClearMask | 8;
    }
    Reader reader(_addressMap);
    Offset fastBinLimit = arenaAddress + _arenaTopOffset;
    for (Offset fastBinCheck = arenaAddress + 2 * sizeof(int);
         fastBinCheck < fastBinLimit; fastBinCheck += OFFSET_SIZE) {
      int loopGuard = 0;
      try {
        for (Offset listNode = reader.ReadOffset(fastBinCheck); listNode != 0;
             listNode = reader.ReadOffset(listNode + 2 * OFFSET_SIZE)) {
          if (++loopGuard == 10000000) {
            break;
          }
          if (listNode > corruptionPoint && listNode <= repairLimit) {
            Offset sizeAndFlags = reader.ReadOffset(listNode + OFFSET_SIZE);
            if (((sizeAndFlags & expectClearMask) == 0) &&
                ((listNode + (sizeAndFlags & ~7)) <= repairLimit)) {
              if (pastArenaCorruption == 0 || listNode < pastArenaCorruption) {
                pastArenaCorruption = listNode;
              }
            }
          }
        }
      } catch (NotMapped&) {
      }
    }
    for (Offset listHeader =
             arenaAddress + _arenaDoublyLinkedFreeListOffset - 2 * OFFSET_SIZE;
         ; listHeader += (2 * OFFSET_SIZE)) {
      try {
        Offset listNode = reader.ReadOffset(listHeader + 2 * OFFSET_SIZE);
        if (listNode == listHeader) {
          continue;
        }
        if (reader.ReadOffset(listNode + 3 * OFFSET_SIZE) != listHeader) {
          break;
        }
        do {
          if (listNode > corruptionPoint && listNode <= repairLimit) {
            Offset sizeAndFlags = reader.ReadOffset(listNode + OFFSET_SIZE);
            if (((sizeAndFlags & expectClearMask) == 0) &&
                ((listNode + (sizeAndFlags & ~7)) <= repairLimit)) {
              if (pastArenaCorruption == 0 || listNode < pastArenaCorruption) {
                pastArenaCorruption = listNode;
              }
            }
          }
          Offset nextNode = reader.ReadOffset(listNode + 2 * OFFSET_SIZE);
          if (reader.ReadOffset(nextNode + 3 * OFFSET_SIZE) != listNode) {
            break;
          }
          listNode = nextNode;
        } while (listNode != listHeader);
      } catch (NotMapped&) {
      }
    }
    return pastArenaCorruption;
  }

  Offset HandleMainArenaCorruption(Offset corruptionPoint) {
    std::cerr << "Corruption was found in main arena run near 0x" << std::hex
              << corruptionPoint << "\n";
    std::cerr << "Corrupt arena is at 0x" << std::hex << _mainArenaAddress
              << "\n";
    // TODO: This repairLimit is probably too cautious.
    Offset repairLimit = (corruptionPoint & ~0xfff) + 0x2000;
    return SkipArenaCorruption(_mainArenaAddress, corruptionPoint, repairLimit);
  }
  /*
   * Note that the checks can be more strict here because the allocations
   * are known to be in the main arena.
   */

  void AddAllocationsForMainArenaPageRun(Offset base, Offset size) {
    Offset limit = base + size;
    Reader reader(_addressMap);
    Offset sizeAndFlags = reader.ReadOffset(base + OFFSET_SIZE);
    Offset chunkSize = 0;
    Offset prevCheck = base;
    for (Offset check = base; check != limit;
         prevCheck = check, check += chunkSize) {
      if ((sizeAndFlags & (OFFSET_SIZE | 6)) != 0) {
        check = HandleMainArenaCorruption(prevCheck);
        if (check != 0) {
          chunkSize = 0;
          sizeAndFlags = reader.ReadOffset(check + OFFSET_SIZE);
          continue;
        }
        return;
      }
      chunkSize = sizeAndFlags & ~7;

      if ((chunkSize == 0) || (chunkSize >= 0x10000000) ||
          (chunkSize > (limit - check))) {
        check = HandleMainArenaCorruption(prevCheck);
        if (check != 0) {
          chunkSize = 0;
          sizeAndFlags = reader.ReadOffset(check + OFFSET_SIZE);
          continue;
        }
        return;
      }
      Offset allocationSize = chunkSize - OFFSET_SIZE;
      bool isFree = true;
      if (check + chunkSize == limit) {
        allocationSize -= OFFSET_SIZE;
      } else {
        sizeAndFlags = reader.ReadOffset(check + OFFSET_SIZE + chunkSize);
        isFree = ((sizeAndFlags & 1) == 0);
      }
      if (isFree) {
        RecordFree(check + 2 * OFFSET_SIZE, allocationSize);
      } else {
        RecordAllocated(check + 2 * OFFSET_SIZE, allocationSize);
      }
    }
  }

  Offset HandleNonMainArenaCorruption(const Heap& heap,
                                      Offset corruptionPoint) {
    std::cerr << "Corruption was found in non-main arena run near 0x"
              << std::hex << corruptionPoint << "\n";
    Offset arenaAddress = heap._arenaAddress;
    Offset heapAddress = heap._address;
    std::cerr << "Corrupt heap is at 0x" << std::hex << heapAddress << "\n";
    std::cerr << "Corrupt arena is at 0x" << std::hex << arenaAddress << "\n";
    Offset heapLimit = heapAddress + heap._size;
    return SkipArenaCorruption(arenaAddress, corruptionPoint, heapLimit);
  }

  void AddAllocationsForHeap(const Heap& heap) {
    Offset base = heap._address;
    Offset size = heap._size;
    Offset limit = base + size;
    if ((heap._arenaAddress & ~(_maxHeapSize - 1)) == base) {
      base += 4 * OFFSET_SIZE + _arenaStructSize;
    } else {
      base += 4 * OFFSET_SIZE;
    }

    Reader reader(_addressMap);
    Offset sizeAndFlags = reader.ReadOffset(base + OFFSET_SIZE);
    Offset chunkSize = 0;
    Offset prevCheck = base;
    Offset checkLimit = limit - 4 * OFFSET_SIZE;
    for (Offset check = base; check < checkLimit;
         prevCheck = check, check += chunkSize) {
      if (((sizeAndFlags & 2) != 0) ||
          ((OFFSET_SIZE == 8) && ((sizeAndFlags & OFFSET_SIZE) != 0))) {
        check = HandleNonMainArenaCorruption(heap, prevCheck);
        if (check != 0) {
          chunkSize = 0;
          sizeAndFlags = reader.ReadOffset(check + OFFSET_SIZE);
          continue;
        }
        return;
      }
      chunkSize = sizeAndFlags & ~7;
      if ((chunkSize == 0) || (chunkSize >= 0x10000000) ||
          (chunkSize > (limit - check))) {
        check = HandleNonMainArenaCorruption(heap, prevCheck);
        if (check != 0) {
          chunkSize = 0;
          sizeAndFlags = reader.ReadOffset(check + OFFSET_SIZE);
          continue;
        }
        return;
      }
      Offset allocationSize = chunkSize - OFFSET_SIZE;
      bool isFree = true;
      if (check + chunkSize == limit) {
        allocationSize -= OFFSET_SIZE;
      } else {
        sizeAndFlags = reader.ReadOffset(check + OFFSET_SIZE + chunkSize);
        isFree =
            ((sizeAndFlags & 1) == 0) || (allocationSize < 3 * OFFSET_SIZE);
      }
      if ((check + allocationSize + 3 * OFFSET_SIZE == limit) &&
          ((sizeAndFlags & ~7) == 0)) {
        break;
      }
      if (isFree) {
        RecordFree(check + 2 * OFFSET_SIZE, allocationSize);
      } else {
        RecordAllocated(check + 2 * OFFSET_SIZE, allocationSize);
      }
    }
  }

  void FindAllAllocations() {
    LargeAllocationsConstIterator itLarge = _largeAllocations.begin();
    LargeAllocationsConstIterator itLargeEnd = _largeAllocations.end();
    HeapMapConstIterator itHeaps = _heaps.begin();
    HeapMapConstIterator itHeapsEnd = _heaps.end();
    MainArenaPagesConstIterator itPages = _mainArenaPages.begin();
    MainArenaPagesConstIterator itPagesEnd = _mainArenaPages.end();
    while (itLarge != itLargeEnd) {
      if (itHeaps != itHeapsEnd) {
        if (itPages != itPagesEnd) {
          if (itLarge->first < itHeaps->first) {
            if (itLarge->first < itPages->first) {
              AddLargeAllocation(itLarge->first, itLarge->second);
              ++itLarge;
            } else {
              AddAllocationsForMainArenaPageRun(itPages->first,
                                                itPages->second);
              ++itPages;
            }
          } else {
            if (itHeaps->first < itPages->first) {
              AddAllocationsForHeap(itHeaps->second);
              ++itHeaps;
            } else {
              AddAllocationsForMainArenaPageRun(itPages->first,
                                                itPages->second);
              ++itPages;
            }
          }

        } else {
          if (itLarge->first < itHeaps->first) {
            AddLargeAllocation(itLarge->first, itLarge->second);
            ++itLarge;
          } else {
            AddAllocationsForHeap(itHeaps->second);
            ++itHeaps;
          }
        }
      } else {
        if (itPages != itPagesEnd) {
          if (itLarge->first < itPages->first) {
            AddLargeAllocation(itLarge->first, itLarge->second);
            ++itLarge;
          } else {
            AddAllocationsForMainArenaPageRun(itPages->first, itPages->second);
            ++itPages;
          }
        } else {
          for (; itLarge != itLargeEnd; ++itLarge) {
            AddLargeAllocation(itLarge->first, itLarge->second);
          }
          return;
        }
      }
    }
    while (itHeaps != itHeapsEnd) {
      if (itPages != itPagesEnd) {
        if (itHeaps->first < itPages->first) {
          AddAllocationsForHeap(itHeaps->second);
          ++itHeaps;
        } else {
          AddAllocationsForMainArenaPageRun(itPages->first, itPages->second);
          ++itPages;
        }
      } else {
        for (; itHeaps != itHeapsEnd; ++itHeaps) {
          AddAllocationsForHeap(itHeaps->second);
        }
      }
    }
    for (; itPages != itPagesEnd; ++itPages) {
      AddAllocationsForMainArenaPageRun(itPages->first, itPages->second);
    }
  }

  Offset ArenaAddressFor(Offset address) {
    HeapMapConstIterator it = _heaps.find(address & ~(_maxHeapSize - 1));
    return (it != _heaps.end()) ? it->second._arenaAddress : _mainArenaAddress;
  }

   void ReportFastBinCorruption(Arena& arena,
                                Offset fastBinHeader,
                                Offset node,
                                const char *specificError) {
    if (!arena._hasFastBinCorruption) {
      arena._hasFastBinCorruption = true;
      std::cerr << "Fast bin corruption was found for the arena"
                   " at 0x"
                << std::hex << arena._address << "\n";
      std::cerr << "  Leak analysis will not be accurate.\n";
      std::cerr << "  Used/free analysis will not be accurate "
                   "for the arena.\n";
    }
    std::cerr << "  The fast bin list headed at 0x"
              << std::hex
              << fastBinHeader
              << " has a node\n  0x" << node
              << " "
              << specificError << ".\n";
  }

  void CorrectFreeListStatus() {
    AllocationIndex noAllocation = _allocations.size();
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Offset arenaAddress = it->first;
      Arena& arena = it->second;
      Offset fastBinLimit = arenaAddress + _arenaTopOffset;
      Reader reader(_addressMap);
      for (Offset fastBinCheck = arenaAddress + 2 * sizeof(int);
           fastBinCheck < fastBinLimit; fastBinCheck += OFFSET_SIZE) {
        try {
          for (Offset nextNode = reader.ReadOffset(fastBinCheck); nextNode != 0;
               nextNode = reader.ReadOffset(nextNode + OFFSET_SIZE * 2)) {
            Offset allocation = nextNode + OFFSET_SIZE * 2;
            AllocationIndex index = AllocationIndexOf(allocation);
            if (index == noAllocation ||
                _allocations[index].Address() != allocation) {
              ReportFastBinCorruption(arena,
                                      fastBinCheck,
                                      nextNode,
                                      "not matching an allocation");
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
            if (ArenaAddressFor(nextNode) != arenaAddress) {
              ReportFastBinCorruption(arena,
                                      fastBinCheck,
                                      nextNode,
                                      "in the wrong arena");
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
            _allocations[index].MarkAsFree();
          }
        } catch (NotMapped& e) {
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
          ReportFastBinCorruption(arena, fastBinCheck, e._address,
                                  "not in the core");
        }
      }
    }
  }
  // ??? make sure to include logic related to registers and
  // ??? stacks for arenas in flux
  void ReportFreeListCorruption(Arena& arena, Offset freeListHeader,
                                Offset node, const char* specificError) {
    if (!arena._hasFreeListCorruption) {
      arena._hasFastBinCorruption = true;
      std::cerr << "Doubly linked free list corruption was "
                   "found for the arena"
                   " at 0x"
                << std::hex << arena._address << "\n";
      std::cerr << "  Leak analysis may not be accurate.\n";
      std::cerr << "  Used/free analysis may not be accurate "
                   "for the arena.\n";
    }
    std::cerr << "  The free list headed at 0x" << std::hex << freeListHeader
              << " has a node\n  0x" << node << " " << specificError << ".\n";
  }

  void CheckForDoublyLinkedListCorruption() {
    AllocationIndex noAllocation = _allocations.size();
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Offset arenaAddress = it->first;
      Offset firstList =
          arenaAddress + _arenaDoublyLinkedFreeListOffset - 2 * OFFSET_SIZE;
      Offset lastList =
          arenaAddress + _arenaLastDoublyLinkedFreeListOffset - 2 * OFFSET_SIZE;
      Arena& arena = it->second;
      for (Offset list = firstList; list <= lastList; list += 2 * OFFSET_SIZE) {
        try {
          Offset firstNode = reader.ReadOffset(list + 2 * OFFSET_SIZE);
          Offset lastNode = reader.ReadOffset(list + 3 * OFFSET_SIZE);
          if (firstNode == list) {
            if (lastNode != list) {
              ReportFreeListCorruption(arena, list + 2 * OFFSET_SIZE,
                                       lastNode,
                                       "at end of list with empty start");
            }
          } else {
            if (lastNode == list) {
              ReportFreeListCorruption(arena, list + 2 * OFFSET_SIZE,
                                       lastNode,
                                       "at start of list with empty end");
            } else {
              Offset prevNode = list;
              for (Offset node = firstNode; node != list;
                   node = reader.ReadOffset(node + 2 * OFFSET_SIZE)) {
                Offset allocation = node + 2 * OFFSET_SIZE;
                AllocationIndex index = AllocationIndexOf(allocation);
                if (index == noAllocation) {
                  ReportFreeListCorruption(arena, list + 2 * OFFSET_SIZE,
                                           node,
                                           "not matching an allocation");
                  break;
                }
                Offset allocationSize = AllocationAt(index)->Size();
                if ((reader.ReadOffset(allocation + allocationSize) & 1) != 0) {
                  ReportFreeListCorruption(arena, list + 2 * OFFSET_SIZE, node,
                                           "with a wrong used/free status bit");
                  break;
                }
                if (ArenaAddressFor(node) != arenaAddress) {
                  ReportFreeListCorruption(arena, list + 2 * OFFSET_SIZE,
                                           node,
                                           "in the wrong arena");
                  break;
                }
                if (reader.ReadOffset(node + 3 * OFFSET_SIZE) != prevNode) {
                  ReportFreeListCorruption(arena, list + 2 * OFFSET_SIZE,
                                           node,
                                           "with an unexpected back pointer");
                  break;
                }
                prevNode = node;
              }
            }
          }
        } catch (NotMapped& e) {
          ReportFreeListCorruption(arena, list + 2 * OFFSET_SIZE,
                                   e._address,
                                   "not in the core");
        }
      }
    }
  }
  void CheckForCorruption() { CheckForDoublyLinkedListCorruption(); }
};

}  // namespace Linux
}  // namespace chap
