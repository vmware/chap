// Copyright (c) 2017-2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Allocations/Directory.h"
#include "../ModuleDirectory.h"
#include "../UnfilledImages.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace chap {
namespace LibcMalloc {
template <class Offset>
class InfrastructureFinder {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;

  InfrastructureFinder(VirtualMemoryPartition<Offset>& virtualMemoryPartition,
                       const ModuleDirectory<Offset>& moduleDirectory,
                       UnfilledImages<Offset>& unfilledImages)
      : LIBC_MALLOC_HEAP("libc malloc heap"),
        LIBC_MALLOC_HEAP_TAIL_RESERVATION("libc malloc heap tail reservation"),
        LIBC_MALLOC_MAIN_ARENA("libc malloc main arena"),
        LIBC_MALLOC_MAIN_ARENA_PAGES("libc malloc main arena pages"),
        _virtualMemoryPartition(virtualMemoryPartition),
        _moduleDirectory(moduleDirectory),
        _unfilledImages(unfilledImages),
        _addressMap(virtualMemoryPartition.GetAddressMap()),
        _mainArenaAddress(0),
        _mainArenaIsContiguous(false),
        _completeArenaRingFound(false),
        _unfilledImagesFound(false),
        _fastBinLinksAreMangled(false),
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
        FindNonMainArenasByRingFromMainArena();
      }

    } else {
      /*
       * At least one non-main arena is present.  That means, if the
       * core is complete, that we expect to find a ring containing
       * at least two arenas, one of which was the main arena.
       */

      if (!FindArenasByRingFromNonMainArenas()) {
        /*
         * It was not possible to complete the ring, least not
         * based on the default maximum heap size.
         */

        if (ScanForMainArena()) {
          /*
           * The main arena was found.  Perhaps someone reduced the
           * default max heap size at compilation time.  Check that.
           */
          FindNonMainArenasByRingFromMainArena();
        }
      }
    }

    if (!_completeArenaRingFound) {
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

      if (!DeriveArenaOffsets(true)) {
        abort();
      }

      if (_mainArenaAddress != 0 && !hadMainArenaBeforeDerivation) {
        Reader reader(_addressMap);
        try {
          Offset nextArena =
              reader.ReadOffset(_mainArenaAddress + _arenaNextOffset);
          Offset top = reader.ReadOffset(_mainArenaAddress + _arenaTopOffset);
          Offset size = reader.ReadOffset(_mainArenaAddress + _arenaSizeOffset);
          Offset maxSize =
              reader.ReadOffset(_mainArenaAddress + _arenaMaxSizeOffset);
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
          mainArena._maxSize = maxSize;
          _mainArenaIsContiguous = isContiguous;
        } catch (NotMapped&) {
          std::cerr << "Derived main arena address at " << std::hex
                    << _mainArenaAddress << " appears to be suspect."
                    << std::endl;
          std::cerr << "One possibility is an incomplete core." << std::endl;
        }
      }
      /*
       * For detected heaps that do not refer to valid non-main arenas
       * do further checking to see whether the issue is an arena that
       * is missing from the core or whether the heap is actually
       * appears to be invalid.  The count can't go to 0 because
       * there is at least one valid heap per detected non-main arena.
       */

      CheckHeapArenaReferences();

      /*
       * Given that the full arena was not found, some of the arena nexts
       * may point to areas that never got copied into images in the core
       * or (much less likely) might be corrupt.
       */

      CheckArenaNexts();
      if (_arenas.size() == 0) {
        if (_heaps.size() > 0) {
          std::cerr << "Failed to find any arenas, main or not." << std::endl;
          std::cerr << "However, " << std::dec << _heaps.size()
                    << " heaps were found.\n";
          std::cerr << "An attempt will be made to used this partial "
                       " information.\n";
          std::cerr
              << "Leaked status and used/free status cannot be trusted.\n";
        } else {
          /*
           * No arenas or heaps were found at all.  It will not be possible
           * to find any allocations.
           */
          return;
        }
      }
    }

    /*
     * Whether or not the full arena ring has been found, for the arenas that
     * are known we haven't verified that the top values are sound and in the
     * case of a non-main-arena we also need to check whether all the heaps are
     * present.
     */

    CheckArenaTops();

    /*
     * Now that the set of heap ranges is roughly trusted, it is good to mark
     * them so that they don't need to be scanned unnecessarily for other
     * possible uses.
     */

    ClaimHeapRanges();

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
      _virtualMemoryPartition.ClearStaticAnchorCandidates(_mainArenaAddress,
                                                          approximateArenaSize);
    }

    /*
     * If we have reached this point we have found the address of at least
     * one arena, and so it is likely that libc malloc is in use.  Even
     * if the main arena was not actually found it is expected to have been
     * present in the process and it will still be necessary to find any
     * allocations associated with the main arena.
     */

    FindMainArenaRuns();
  }

  const char* LIBC_MALLOC_HEAP;
  const char* LIBC_MALLOC_HEAP_TAIL_RESERVATION;
  const char* LIBC_MALLOC_MAIN_ARENA;
  const char* LIBC_MALLOC_MAIN_ARENA_PAGES;

  struct Arena {
    Arena(Offset address)
        : _address(address),
          _nextArena(0),
          _top(0),
          _size(0),
          _maxSize(0),
          _freeCount(0),
          _freeBytes(0),
          _usedCount(0),
          _usedBytes(0),
          _hasFastBinCorruption(false),
          _hasFreeListCorruption(false),
          _missingOrUnfilledHeader(false) {}
    Offset _address;
    Offset _nextArena;
    Offset _top;
    Offset _size;
    Offset _maxSize;
    Offset _freeCount;
    Offset _freeBytes;
    Offset _usedCount;
    Offset _usedBytes;
    bool _hasFastBinCorruption;
    bool _hasFreeListCorruption;  // ... in doubly linked list
    bool _missingOrUnfilledHeader;
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
  typedef std::map<Offset, Offset> MainArenaRuns;
  typedef typename MainArenaRuns::iterator MainArenaRunsIterator;
  typedef typename MainArenaRuns::const_iterator MainArenaRunsConstIterator;

  const ArenaMap& GetArenas() const { return _arenas; }
  Offset GetMainArenaAddress() const { return _mainArenaAddress; }
  Offset GetArenaStructSize() const { return _arenaStructSize; }
  Offset GetFastBinStartOffset() const { return _fastBinStartOffset; }
  Offset GetFastBinLimitOffset() const { return _fastBinLimitOffset; }
  bool FastBinLinksAreMangled() const { return _fastBinLinksAreMangled; }
  Offset GetArenaDoublyLinkedFreeListOffset() const {
    return _arenaDoublyLinkedFreeListOffset;
  }
  Offset GetArenaLastDoublyLinkedFreeListOffset() const {
    return _arenaLastDoublyLinkedFreeListOffset;
  }
  const HeapMap& GetHeaps() const { return _heaps; }
  Offset GetMaxHeapSize() const { return _maxHeapSize; }
  const MainArenaRuns& GetMainArenaRuns() const { return _mainArenaRuns; }
  Offset ArenaAddressFor(Offset address) const {
    HeapMapConstIterator itHeaps = _heaps.find(address & ~(_maxHeapSize - 1));
    if (itHeaps != _heaps.end()) {
      return itHeaps->second._arenaAddress;
    }

    MainArenaRunsConstIterator itRuns = _mainArenaRuns.upper_bound(address);

    if (itRuns == _mainArenaRuns.begin()) {
      return 0;
    }

    --itRuns;
    Offset runStart = itRuns->first;
    if (address < runStart) {
      return 0;
    }
    Offset runLimit = runStart + itRuns->second;
    if (address >= runLimit) {
      return 0;
    }

    return _mainArenaAddress;
  }

 private:
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const ModuleDirectory<Offset>& _moduleDirectory;
  UnfilledImages<Offset>& _unfilledImages;
  const VirtualAddressMap<Offset>& _addressMap;

  static constexpr Offset OFFSET_SIZE = sizeof(Offset);
  static constexpr Offset DEFAULT_MAX_HEAP_SIZE =
      (OFFSET_SIZE == 4) ? 0x100000 : 0x4000000;

  HeapMap _heaps;
  ArenaMap _arenas;
  MainArenaRuns _mainArenaRuns;
  Offset _mainArenaAddress;
  bool _mainArenaIsContiguous;
  bool _completeArenaRingFound;
  bool _unfilledImagesFound;
  bool _fastBinLinksAreMangled;
  Offset _arenaNextOffset;
  Offset _arenaSizeOffset;
  Offset _arenaMaxSizeOffset;
  Offset _fastBinStartOffset;
  Offset _fastBinLimitOffset;
  Offset _arenaTopOffset;
  Offset _arenaDoublyLinkedFreeListOffset;
  Offset _arenaLastDoublyLinkedFreeListOffset;
  Offset _arenaStructSize;
  Offset _maxHeapSize;

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
      Offset top = reader.ReadOffset(arena._address + candidate, 0);
      if (top != 0) {
        Offset topSizeAndFlags = reader.ReadOffset(top + OFFSET_SIZE, 0);
        if ((topSizeAndFlags != 0) &&
            (((top + (topSizeAndFlags & ~7)) & 0xFFF) == 0)) {
          numVotes++;
        }
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
      Offset first = reader.ReadOffset(adjustedHeader + OFFSET_SIZE * 2, 0);
      Offset last = reader.ReadOffset(adjustedHeader + OFFSET_SIZE * 3, 0);
      if ((first == adjustedHeader && last == adjustedHeader) ||
          (reader.ReadOffset(first + OFFSET_SIZE * 3, 0) == adjustedHeader &&
           reader.ReadOffset(last + OFFSET_SIZE * 2, 0) == adjustedHeader)) {
        numVotes++;
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
      Offset next = reader.ReadOffset(arena._address + candidate, 0);
      if ((next != 0) && ((next & (OFFSET_SIZE - 1)) == 0)) {
        if (_arenas.find(next) != _arenas.end()) {
          numVotes++;
        } else {
          Offset nextNext = reader.ReadOffset(next + candidate, 0);
          if ((nextNext != 0) && (_arenas.find(nextNext) != _arenas.end())) {
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
    }
    return numVotes;
  }

  size_t CheckSizeOffset(Offset candidate) {
    size_t numVotes = 0;
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      Offset size = reader.ReadOffset(arena._address + candidate, 0);
      if (size != 0) {
        Offset maxSize =
            reader.ReadOffset(arena._address + candidate + OFFSET_SIZE, 0);
        if ((maxSize != 0) && (_arenas.find(size) == _arenas.end()) &&
            ((size & 0xFFF) == (maxSize & 0xFFF))) {
          /*
           * Note that for recent libc builds, allocation runs no longer
           * need to start on page boundaries but they still need to end
           * on them.
           */
          numVotes++;
        }
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
      if ((reader.ReadOffset(possibleAllocationStart, ~0) == 0)) {
        Offset sizeAndFlags =
            reader.ReadOffset(possibleAllocationStart + OFFSET_SIZE, 0);
        if ((sizeAndFlags & ~(_maxHeapSize - 4)) == 1) {
          numVotes++;
        }
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

  bool SetArenasBasedOnRing(const std::vector<Offset> arenaAddresses) {
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
     * If all the arenas have been found it should be safe to
     * derive the offsets of various fields.  This also fills in various
     * fields of the Arena objects, such as the _size field, based on the
     * derived offsets.
     * If the derivation of the arena offsets fails it is assumed that the
     * arena ring was guessed incorrectly.
     */

    if (!DeriveArenaOffsets(false)) {
      return false;
    }

    /*
     * At this point the function is always going to return true because
     * the full ring has been found and the arena offsets have been derived
     * successfully.
     */

    _completeArenaRingFound = true;

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
      return true;
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
        return true;
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
            return true;
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
    return true;
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
    Offset limit = _mainArenaAddress + 0x120 * OFFSET_SIZE;
    for (Offset checkAt = _mainArenaAddress + 0x80 * OFFSET_SIZE;
         checkAt < limit; checkAt += OFFSET_SIZE) {
      if (reader.ReadOffset(checkAt, 0xbadbad) == _mainArenaAddress) {
        /*
         * The arena points to itself so there really is just one
         * arena and no non-main arenas exist.
         */
        return false;
      }
    }

    for (Offset checkAt = _mainArenaAddress; checkAt < limit;
         checkAt += OFFSET_SIZE) {
      Offset candidate = reader.ReadOffset(checkAt, 0xbadbad);
      Offset nextOffset = checkAt - _mainArenaAddress;
      std::vector<Offset> candidates;

      if ((candidate & 0xffff) == (4 * OFFSET_SIZE)) {
        do {
          candidates.push_back(candidate);
          candidate = reader.ReadOffset(candidate + nextOffset, 0xbadbad);
        } while ((candidate & 0xffff) == (4 * OFFSET_SIZE));
        if (candidate == _mainArenaAddress) {
          /*
           * We had to have made it at least one time through the ring
           * because the value of candidate before the loop was known
           * not to be _mainArenaAddress.
           */
          candidates.push_back(_mainArenaAddress);
          if (SetArenasBasedOnRing(candidates)) {
            return true;
          }
          /*
           * Reaching this point means that the ring found was a false ring.
           */
        }
      }
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
    (void)_arenas
        .insert(std::make_pair(_mainArenaAddress, Arena(_mainArenaAddress)))
        .first;

    Offset arenaAddress = _mainArenaAddress;
    std::vector<Offset> inRing;
    Reader reader(_addressMap);
    do {
      Offset nextArena =
          reader.ReadOffset(arenaAddress + bestNextOffset, 0xbad);
      if (nextArena == 0xbad) {
        return false;
      }
      inRing.push_back(arenaAddress);
      arenaAddress = nextArena;
      if (arenaAddress == _mainArenaAddress) {
        if (SetArenasBasedOnRing(inRing)) {
          return true;  // The ring was found and verified.
        }
        _mainArenaAddress = 0;
        break;
      }
    } while ((arenaAddress & 0xffff) == (4 * OFFSET_SIZE));
    _mainArenaAddress = 0;
    return false;  // The ring was never found or failed verfication.
  }

  void DeriveFastBinLimits() {
    _fastBinLimitOffset = _arenaTopOffset;
    /*
     * Guess the start of the fast bin lists.  This was made necessary by
     * a change in malloc_state as of libc 2.27.  The guess may be wrong
     * if all the fast bin lists are empty for all the arenas, but in such
     * a case it doesn't matter so much if it is wrong because the
     * the offset is basically to get a bound on the range of fast bin
     * lists to check for free items and corruption, and empty lists don't
     * matter for that.
     */
    _fastBinStartOffset = 2 * sizeof(int);
    size_t votesForFirstOffset = 0;
    size_t votesForSecondOffset = 0;
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      if (reader.ReadU8(arena._address + _fastBinStartOffset, 0) ==
          ((uint8_t)(1))) {
        votesForSecondOffset++;
      } else {
        Offset firstOnList =
            reader.ReadOffset(arena._address + _fastBinStartOffset, 0);
        if (firstOnList != 0) {
          Offset sizeAndStatus =
              reader.ReadOffset(firstOnList + OFFSET_SIZE, 0);
          if (sizeAndStatus / (2 * OFFSET_SIZE) == 2) {
            votesForFirstOffset++;
          }
        }
      }
      Offset expectForSecondOffset = 2;
      for (Offset inFastBin = _fastBinStartOffset + OFFSET_SIZE;
           inFastBin < _fastBinLimitOffset; inFastBin += OFFSET_SIZE) {
        Offset firstOnList = reader.ReadOffset(arena._address + inFastBin, 0);
        if (firstOnList != 0) {
          Offset sizeAndStatus =
              reader.ReadOffset(firstOnList + OFFSET_SIZE, 0);
          Offset indexPlus2 = sizeAndStatus / (2 * OFFSET_SIZE);
          if (indexPlus2 == expectForSecondOffset) {
            votesForSecondOffset++;
          } else if (indexPlus2 == expectForSecondOffset + 1) {
            votesForFirstOffset++;
          }
        }
        expectForSecondOffset++;
      }
    }
    if (votesForSecondOffset > votesForFirstOffset) {
      _fastBinStartOffset += OFFSET_SIZE;
    }
  }

  bool CheckForFastBinLinkMangling() {
    size_t votesForMangling = 0;
    size_t votesAgainstMangling = 0;
    Reader reader(_addressMap);
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      for (Offset inFastBin = _fastBinStartOffset;
           inFastBin < _fastBinLimitOffset; inFastBin += OFFSET_SIZE) {
        if (votesForMangling > 10 && votesAgainstMangling == 0) {
          return true;
        }
        if (votesAgainstMangling > 10 && votesForMangling == 0) {
          return false;
        }
        Offset firstOnList = reader.ReadOffset(arena._address + inFastBin, 0);
        if (firstOnList == 0) {
          // The list is empty or perhaps the arena header is unreadable.
          continue;
        }
        Offset linkAddr = firstOnList + 2 * OFFSET_SIZE;
        Offset nextOnList = reader.ReadOffset(linkAddr, (Offset)(~0));
        if (nextOnList == 0) {
          // This ought to indicate that no mangling is present, unless the
          // link was corrupted with a 0.
          votesAgainstMangling += 3;
          continue;
        }
        Offset nextOnListXor = linkAddr >> 12;
        if (nextOnList == nextOnListXor) {
          // This almost certainly indicates that mangling is present unless
          // the link was corrupted with that one peculiar value, which
          // seems rather unlikely.
          votesForMangling += 7;
          continue;
        }

        if (((nextOnList ^ nextOnListXor) & (2 * OFFSET_SIZE - 1)) == 0) {
          while (nextOnList != nextOnListXor) {
            linkAddr = nextOnList + 2 * OFFSET_SIZE;
            nextOnList =
                reader.ReadOffset(nextOnList ^ nextOnListXor, (Offset)(~0));
            if (nextOnList == (Offset)(~0)) {
              break;
            }
            nextOnListXor = linkAddr >> 12;
            votesForMangling++;
          }
          if (nextOnList == nextOnListXor) {
            votesForMangling += 9;
            continue;
          }
        }
        if ((nextOnList & (2 * OFFSET_SIZE - 1)) != 0) {
          while (nextOnList != 0) {
            nextOnList = reader.ReadOffset(nextOnList, (Offset)(~0));
            if (nextOnList == (Offset)(~0)) {
              break;
            }
            votesAgainstMangling++;
          }
          if (nextOnList == 0) {
            votesAgainstMangling += 5;
          }
        }
      }
    }
    /*
     * We might have a wrong result if there are no non-empty fast bin lists
     * but this doesn't particularly matter because we have no links to
     * traverse.  A false result could happen if the only non-empty
     * fast bin list or lists were corrupt in the first link, but this seems
     * very unlikely and is probably not a big deal given that corruption
     * will at least be caught.
     */
    if (votesForMangling == 0) {
      return false;
    }
    if (votesAgainstMangling == 0) {
      return true;
    }

    return votesForMangling > votesAgainstMangling;
  }

  bool DeriveArenaOffsets(bool showErrors) {
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
      if (numBadTops > 0 && showErrors) {
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

    DeriveFastBinLimits();

    _fastBinLinksAreMangled = CheckForFastBinLinkMangling();

    if (numListOffsetVotes < numArenas) {
      if (numListOffsetVotes == 0) {
        if (showErrors) {
          std::cerr << "The arena format is totally unrecognized.\n";
        }
        return false;
      } else {
        if (showErrors) {
          std::cerr << "At least one arena has an invalid doubly linked list"
                    << " at offset 0x" << std::hex
                    << _arenaDoublyLinkedFreeListOffset;
        }
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

    for (Offset nextOffset =
             _arenaLastDoublyLinkedFreeListOffset + 2 * OFFSET_SIZE;
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
        if (showErrors) {
          std::cerr << "The arena next pointer was not found.\n";
          std::cerr << "Scanning started at offset 0x" << std::hex
                    << (_arenaLastDoublyLinkedFreeListOffset + 2 * OFFSET_SIZE)
                    << " and applied to the following arenas:\n";
          for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end();
               ++it) {
            std::cerr << "0x" << it->first << "\n";
          }
        }
        return false;
      } else {
        if (showErrors) {
          std::cerr << "At least one arena has an invalid next pointer"
                    << " at offset 0x" << std::hex << _arenaNextOffset
                    << std::endl;
        }
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
        _arenaMaxSizeOffset = sizeOffset + sizeof(Offset);
        if (numVotes == numArenas) {
          break;
        }
      }
    }
    if (bestSizeOffsetVotes < numArenas) {
      if (bestSizeOffsetVotes == 0) {
        if (showErrors) {
          std::cerr << "The arena size field was not found.\n";
        }
        return false;
      } else {
        if (showErrors) {
          std::cerr << "At least one arena has an invalid arena size field"
                    << " at offset 0x" << std::hex << _arenaSizeOffset
                    << std::endl;
        }
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
          if (showErrors) {
            std::cerr << "The arena structure size was not derived.\n";
          }
          return false;
        } else {
          if (showErrors) {
            std::cerr << "At least one arena has an invalid heap start.\n";
          }
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
        arena._maxSize = reader.ReadOffset(arenaAddress + _arenaMaxSizeOffset);
      } catch (NotMapped&) {
        if (showErrors) {
          std::cerr << "Arena at " << std::hex << arenaAddress
                    << " is not fully mapped.\n";
        }
      }
    }
    return true;
  }
  void SetUnfilledImagesFound() {
    if (!_unfilledImagesFound) {
      _unfilledImagesFound = true;
      std::cerr << "Apparently this core file was not completely filled in.\n"
                << "Probably the process was killed while the core was being "
                   "generated.\n"
                << "As a result any commands related to allocations will be "
                   "very inaccurate.\n";
    }
  }
  bool CheckUnfilledHeapStart(Offset address) {
    if (_unfilledImages.RegisterIfUnfilled(
            address, _maxHeapSize, LIBC_MALLOC_HEAP) == LIBC_MALLOC_HEAP) {
      SetUnfilledImagesFound();
      return true;
    }
    return false;
  }
  bool CheckUnfilledMainArenaStartPage(Offset address) {
    if (_unfilledImages.RegisterIfUnfilled(
            address, 1, LIBC_MALLOC_MAIN_ARENA) == LIBC_MALLOC_MAIN_ARENA) {
      SetUnfilledImagesFound();
      return true;
    }
    return false;
  }
  bool CheckUnfilledArenaStart(Offset address) {
    return ((address & (_maxHeapSize - 1)) == (4 * sizeof(Offset)))
               ? CheckUnfilledHeapStart(address & ~(_maxHeapSize - 1))
               : CheckUnfilledMainArenaStartPage(address);
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
        Offset chunkAddr = heapAddress + OFFSET_SIZE * 5;
        Offset bytesLeft = _maxHeapSize - OFFSET_SIZE * 5;
        Offset sizeAndFlags = reader.ReadOffset(chunkAddr, 0);
        int numSizesOk = 0;
        for (; numSizesOk < 10; ++numSizesOk) {
          Offset chunkSize = sizeAndFlags & (Offset)(~7);
          if (chunkSize < 4 * sizeof(Offset) || chunkSize > bytesLeft) {
            break;
          }
          chunkAddr += chunkSize;
          bytesLeft -= chunkSize;
          if (bytesLeft == 0) {
            break;
          }
          sizeAndFlags = reader.ReadOffset(chunkAddr, 0);
        }
        if (numSizesOk == 10 || bytesLeft < 2 * sizeof(Offset)) {
          _arenas.insert(std::make_pair(arenaAddress, Arena(arenaAddress)))
              .first->second._missingOrUnfilledHeader = true;
          if (!CheckUnfilledArenaStart(arenaAddress)) {
            /*
             * If the arena was not found because the image of that arena
             * was never filled in in the core, let the checks for unfilled
             * heap starts report that.  Otherwise generate a warning.  Note
             * that missing from the core here is intended to mean entirely
             * unknown in the table of contents.
             */
            std::cerr << "Arena at " << std::hex << arenaAddress
                      << " appears to be "
                      << ((_addressMap.find(arenaAddress) == _addressMap.end())
                              ? "missing from the core."
                              : "corrupt.")
                      << std::endl
                      << "Leak analysis will not be reliable." << std::endl;
          }
          ++it;
          continue;
        }
        std::cerr << "Ignoring false heap at " << std::hex << heapAddress
                  << std::endl;
        _heaps.erase(it++);
      } else {
        ++it;
      }
    }
  }

  void CheckMainArenaTop(Arena&) {
    // TODO: check for a 0 filled page in arena run here
  }

  /*
   * Check the top of the given non-main arena and report any errors found.
   * A side effect may be that the core is detected as not being completely
   * filled in.
   */
  void CheckNonMainArenaTop(Arena& arena) {
    Offset arenaHeapAddr = arena._address & ~(_maxHeapSize - 1);
    Offset topHeapAddr = arena._top & ~(_maxHeapSize - 1);
    for (Offset heapAddr = topHeapAddr; heapAddr != arenaHeapAddr;) {
      HeapMapIterator it = _heaps.find(heapAddr);
      if (it == _heaps.end()) {
        // We don't know about this heap yet.
        if (!CheckUnfilledHeapStart(heapAddr)) {
          /*
           * If the reason we don't know about the heap is that the image
           * in the core never got filled in, let the logic to check that
           * report it.  Otherwise, report the error here.
           */
          if (heapAddr == topHeapAddr) {
            std::cerr << "Arena at 0x" << std::hex << arena._address
                      << " appears to have an invalid top address 0x"
                      << arena._top << "\n";
          } else {
            /*
             * The last heap was already found, so we consider the arena
             * to be reasonable.
             */
            std::cerr << "Arena at 0x" << std::hex << arena._address
                      << " appears to have a corrupt or missing heap at 0x"
                      << heapAddr << "\n";
          }
        }
        break;
      }
      heapAddr = it->second._nextHeap;
    }
  }

  void CheckArenaTops() {
    /*
     * Scan just the heap-based arenas, expecting every top value to reside
     * within one of the allocated heaps. For now, discard any heaps where
     * this doesn't match but it would be better to allow calculations to
     * continue if not.
     */

    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      if (!arena._missingOrUnfilledHeader) {
        if (arena._address == _mainArenaAddress) {
          CheckMainArenaTop(arena);
        } else {
          CheckNonMainArenaTop(arena);
        }
      }
    }
  }

  void CheckArenaNexts() {
    for (ArenaMapIterator it = _arenas.begin(); it != _arenas.end(); ++it) {
      Arena& arena = it->second;
      if (arena._missingOrUnfilledHeader) {
        continue;
      }
      Offset nextArena = arena._nextArena;
      if (_arenas.find(nextArena) == _arenas.end()) {
        /*
         * We have a pointer for the next arena but it wasn't detected as
         * an arena.
         */
        if (CheckUnfilledArenaStart(nextArena)) {
          /* If it appears that the arena was not detected because the image
           * of
           * the arena was never filled in, let the logic that checks for such
           * unfilled areas report it.  Otherwise, report it here.
           */
          std::cerr << std::hex << "Arena at 0x" << arena._address
                    << " has questionable next pointer 0x" << nextArena
                    << std::endl;
          std::cerr << "The core may be incomplete and leak analysis "
                    << " is compromised" << std::endl;
        }
      }
    }
  }

  bool IsEmptyDoubleFreeList(Reader& reader, Offset listAddr) {
    return reader.ReadOffset(listAddr + 2 * OFFSET_SIZE, 0xbadbad) ==
               listAddr &&
           reader.ReadOffset(listAddr + 3 * OFFSET_SIZE, 0xbadbad) == listAddr;
  }

  bool IsNonEmptyDoubleFreeList(Reader& reader, Offset listAddr) {
    Reader freeReader(_addressMap);
    Offset firstFree = reader.ReadOffset(listAddr + 2 * OFFSET_SIZE, listAddr);
    if (firstFree != listAddr) {
      Offset lastFree = reader.ReadOffset(listAddr + 3 * OFFSET_SIZE, listAddr);
      if (lastFree != listAddr &&
          freeReader.ReadOffset(firstFree + 3 * OFFSET_SIZE, 0xbadbad) ==
              listAddr &&
          freeReader.ReadOffset(lastFree + 2 * OFFSET_SIZE, 0xbadbad) ==
              listAddr) {
        return true;
      }
    }
    return false;
  }
  bool HasPlausibleTop(Reader& reader, Offset candidateTopField) {
    Offset top = reader.ReadOffset(candidateTopField);
    Offset topSizeAndFlags = reader.ReadOffset(top + OFFSET_SIZE, 0xbadbad);
    Offset topSize = topSizeAndFlags & ~7;
    return ((top + topSize) & 0xfff) == 0;
  }

  bool ScanForMainArenaByEmptyFreeLists(Offset base, Offset limit) {
    Offset mainArenaCandidate = 0;
    Offset minListAddr = base + 13 * OFFSET_SIZE;
    Offset maxListAddr = limit - 4 * OFFSET_SIZE;
    if (minListAddr > maxListAddr || maxListAddr > limit) {
      /*
       * A core that was generated by fuzzing was provided on a chap issue.
       * In that particular core there was a rather strange region with
       * base 1 and limit 2.  I am not at all clear that any such cores will
       * ever happen in practice but if such regions do occur, they can't
       * possibly contain the main arena.
       */
      return false;
    }
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
      if ((heapCandidate & 0xffff) == 0 &&
          mainArenaCandidate == reader.ReadOffset(heapCandidate, 0xbadbad)) {
        isNonMainArena = true;
      }
      if (!isNonMainArena) {
        /*
         * This is a minor hack for the case that the difference between the
         * run base and the arena start was calculated incorrectly.  It
         * needs to be made more robust but for now I am using this to
         * support glibc 2.27.
         */
        for (Offset nextOffset = 0xc0 * sizeof(Offset);
             nextOffset < 0x140 * sizeof(Offset);
             nextOffset += sizeof(Offset)) {
          Offset next =
              reader.ReadOffset(mainArenaCandidate + nextOffset, 0xbad);
          if (next == mainArenaCandidate || next == 0xbad) {
            break;
          }
          if (next == mainArenaCandidate - sizeof(Offset)) {
            mainArenaCandidate -= sizeof(Offset);
            break;
          }
        }
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
      mainArena._maxSize =
          reader.ReadOffset(_mainArenaAddress + 0x10 + 0x10f * OFFSET_SIZE);
      _mainArenaIsContiguous =
          (reader.ReadU32(_mainArenaAddress + sizeof(int)) & 2) == 0;
      return true;
    }
    return false;
  }

  bool ScanForMainArenaInModules(bool libcOnly) {
    for (typename ModuleDirectory<Offset>::const_iterator it =
             _moduleDirectory.begin();
         it != _moduleDirectory.end(); ++it) {
      if (!libcOnly || it->first.find("libc") != std::string::npos) {
        const typename ModuleDirectory<Offset>::RangeToFlags& rangeToFlags =
            it->second;
        for (typename ModuleDirectory<Offset>::RangeToFlags::const_iterator
                 itRange = rangeToFlags.begin();
             itRange != rangeToFlags.end(); ++itRange) {
          int flags = itRange->_value;
          if ((flags & RangeAttributes::IS_WRITABLE) != 0) {
            if (ScanForMainArenaByEmptyFreeLists(itRange->_base,
                                                 itRange->_limit)) {
              return true;
            }
          }
        }
      }
    }
    return false;
  }

  bool ScanForMainArenaInUnclaimedRanges() {
    for (const auto& range :
         _virtualMemoryPartition.GetUnclaimedWritableRangesWithImages()) {
      if (ScanForMainArenaByEmptyFreeLists(range._base, range._limit)) {
        return true;
      }
    }
    return false;
  }

  bool ScanForMainArena() {
    return (_moduleDirectory.IsResolved())
               ? (ScanForMainArenaInModules(true) ||
                  ScanForMainArenaInModules(false))
               : ScanForMainArenaInUnclaimedRanges();
  }

  struct RunCandidate {
    RunCandidate(Offset start, Offset size, Offset numAllocations)
        : _start(start), _size(size), _numAllocations(numAllocations) {}
    Offset _start;
    Offset _size;
    Offset _numAllocations;
  };
  typedef std::vector<RunCandidate> RunCandidates;

  void EvaluateRunCandidate(Offset base, Offset limit,
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
     * arena, not be marked as a mmapped chunk, and have a size that
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
    size_t secondLastCheck = 0;
    size_t thirdLastCheck = 0;
    while (1) {
      if ((check & 0xfff) == 0) {
        lastPageBoundary = check;
        for (typename RunCandidates::reverse_iterator it = candidates.rbegin();
             it != candidates.rend(); ++it) {
          RunCandidate& candidate = *it;
          if (candidate._start == lastPageBoundary) {
            candidate._size += (candidate._start - base);
            candidate._numAllocations += numAllocations;
            candidate._start = base;
            return;
          }
        }
      }
      if (check == limit) {
        // We don't need to add OFFSET_SIZE to check because an invariant
        // here is that both are divisible by 2 * OFFSET_SIZE.
        break;
      }
      Offset sizeAndFlags = reader.ReadOffset(check + OFFSET_SIZE, 0xff);
      if ((sizeAndFlags & (OFFSET_SIZE | 6)) != 0) {
        break;
      }

      Offset chunkSize = sizeAndFlags & ~7;

      Offset nextCheck = check + chunkSize;
      if ((nextCheck <= check) || (nextCheck > limit)) {
        break;
      }

      numAllocations++;
      thirdLastCheck = secondLastCheck;
      secondLastCheck = check;
      check = nextCheck;
    }

    if (numAllocations >= 20 || lastPageBoundary > base) {
      Offset runSize = lastPageBoundary - base;
      if (check != lastPageBoundary && thirdLastCheck > lastPageBoundary) {
        numAllocations -= 2;
        runSize = ((thirdLastCheck + 0xfff) & ~0xfff) - base;
      }
      if (runSize > 0) {
        candidates.emplace_back(base, runSize, numAllocations);
      }
    }
  }

  void ScanForMainArenaRunsInRange(Offset base, Offset limit,
                                   RunCandidates& candidates) {
    limit = limit & ~0xfff;
    base = (base + 0xfff) & ~0xfff;
    RunCandidates candidatesInRange;
    Reader reader(_addressMap);
    for (Offset check = limit - 0x1000; check >= base; check -= 0x1000) {
      EvaluateRunCandidate(check, limit, candidatesInRange);
    }
    for (typename RunCandidates::const_reverse_iterator it =
             candidatesInRange.rbegin();
         it != candidatesInRange.rend(); ++it) {
      candidates.push_back(*it);
    }
  }

  void ScanForMainArenaRuns(Offset mainArenaSize) {
    RunCandidates runCandidates;
    for (const auto& range :
         _virtualMemoryPartition.GetUnclaimedWritableRangesWithImages()) {
      ScanForMainArenaRunsInRange(range._base, range._limit, runCandidates);
    }

    /*
     * Select the _mainArenaRuns from the run candidates.
     */

    size_t numRunCandidates = runCandidates.size();
    if (numRunCandidates == 0) {
      std::cerr << "No main arena runs were found.\n";
      if (_heaps.size() == 0) {
        std::cerr << "Perhaps libc malloc was not used.\n";
      }
      return;
    }

    if (numRunCandidates == 1) {
      std::cerr << "Probably there was a corrupt single main arena run.\n"
                << "Leak analysis probably will not be correct.\n";
      Offset base = runCandidates[0]._start;
      Offset size = runCandidates[0]._size;
      if ((_mainArenaAddress != 0) && (size > mainArenaSize)) {
        size = mainArenaSize;
        // TODO, do this more precisely, taking into account the top
        // value.
      }
      _mainArenaRuns[base] = size;

      if (!_virtualMemoryPartition.ClaimRange(
              base, size, LIBC_MALLOC_MAIN_ARENA_PAGES, false)) {
        std::cerr << "Warning: unexpected overlap for main arena pages at 0x"
                  << std::hex << base << "\n";
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
    for (auto candidate : runCandidates) {
      Offset base = candidate._start;
      Offset size = candidate._size;
      if (base < prevLimit) {
        continue;
      }
      if (!_virtualMemoryPartition.ClaimRange(
              base, size, LIBC_MALLOC_MAIN_ARENA_PAGES, false)) {
        std::cerr << "Warning: unexpected overlap for main arena pages at 0x"
                  << std::hex << base << "\n";
      }
      _mainArenaRuns[base] = size;
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
      _mainArenaIsContiguous = false;
      return false;
    } else {
      RunCandidates runCandidates;
      EvaluateRunCandidate(base, topLimit, runCandidates);
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

      } else if (runCandidates[0]._size != mainArena._size) {
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

    if (!_virtualMemoryPartition.ClaimRange(
            base, mainArena._size, LIBC_MALLOC_MAIN_ARENA_PAGES, false)) {
      std::cerr << "The region " << std::hex << "[0x" << base << ", "
                << topLimit << "] may be inaccurate for main arena pages.\n";
      return false;
    }
    _mainArenaRuns[base] = topLimit - base;
    return true;
  }

  void FindMainArenaRuns() {
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
      mainArenaSize = mainArena._size;
      if (FindSingleContiguousMainArenaRun(mainArena)) {
        return;
      }
    }
    ScanForMainArenaRuns(mainArenaSize);
  }

  void ClaimHeapRanges() {
    typename VirtualAddressMap<Offset>::const_iterator itMapEnd =
        _addressMap.end();
    for (const auto& baseAndInfo : _heaps) {
      Offset heapBase = baseAndInfo.first;
      typename VirtualAddressMap<Offset>::const_iterator itMap =
          _addressMap.find(heapBase);
      if (itMap == itMapEnd) abort();
      Offset limit = itMap.Limit();
      if (limit > heapBase + _maxHeapSize) {
        limit = heapBase + _maxHeapSize;
      }
      if (!_virtualMemoryPartition.ClaimRange(heapBase, limit - heapBase,
                                              LIBC_MALLOC_HEAP, false)) {
        std::cerr << "Warning: unexpected overlap for heap at 0x" << std::hex
                  << heapBase << "\n";
      }

      if (limit < heapBase + _maxHeapSize) {
        ++itMap;
        if ((itMap != itMapEnd) && (itMap.Base() == limit)) {
          int permissions = itMap.Flags() & RangeAttributes::PERMISSIONS_MASK;
          if ((permissions & (RangeAttributes::PERMISSIONS_MASK ^
                              RangeAttributes::IS_READABLE)) !=
              RangeAttributes::HAS_KNOWN_PERMISSIONS) {
            std::cerr
                << "Warning: unexpected permissions for tail for heap at 0x"
                << std::hex << heapBase << "\n";
            continue;
          }
          if ((permissions & RangeAttributes::IS_READABLE) != 0) {
            /*
             * This has been seen in some cores where the tail region
             * has been improperly marked as read-only, even after having
             * been verified as inaccessible at the time the process was
             * running.  We'll grudgingly accept the core's version of the
             * facts here although actually saving images the tail regions can
             * make the core much larger and slower to create.
             */
            if (!_virtualMemoryPartition.ClaimRange(
                    limit, _maxHeapSize - (limit - heapBase),
                    LIBC_MALLOC_HEAP_TAIL_RESERVATION, false)) {
              std::cerr << "Warning: unexpected overlap for tail for heap at 0x"
                        << std::hex << heapBase << "\n";
            }
            continue;
          }
        }
        /*
         * If we reach here, the range was mentioned in the core as
         * inaccessible or not mentioned at all.  The expected thing
         * to do when the core is created is to record the inaccessible
         * tail region in a Phdr, but not to bother providing an image.
         * Unfortunately, some versions of gdb stray from this and either
         * don't have a Phdr or waste core space on an image.
         */
        if (!_virtualMemoryPartition.ClaimRange(
                limit, _maxHeapSize - (limit - heapBase),
                LIBC_MALLOC_HEAP_TAIL_RESERVATION, false)) {
          std::cerr << "Warning: unexpected overlap for tail for heap at 0x"
                    << std::hex << heapBase << "\n";
        }
      }
    }
  }
};

}  // namespace LibcMalloc
}  // namespace chap
