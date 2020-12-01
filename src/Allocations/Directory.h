// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <algorithm>
#include <functional>
#include <vector>
namespace chap {
namespace Allocations {
template <class Offset>
class Directory {
 public:
  typedef unsigned int AllocationIndex;
  class Allocation {
   public:
    /*
     * This constructor is generally used only while the directory is being
     * resolved.  The address, size information and initial guess about
     * whether the allocation is used or free, are supplied by the Finder.
     * The remaining arguments are derived as part of resolving the
     * Directory.
     */
    Allocation(Offset address, Offset size, bool isUsed, Offset finderIndex,
               bool isWrapped)
        : _address(address),
          _sizeAndBits(size | (isUsed ? USED_BIT : 0) |
                       (finderIndex * LOW_FINDER_INDEX_BIT) |
                       (isWrapped ? WRAPPED_BIT : 0)) {}

    /*
     * Mark the given allocation as a wrapper.  This is not allowed after
     * the allocation boundaries are resolved and is enforced by the fact
     * that the directory never provides direct write access to any write
     * allocation.
     */
    void MarkAsWrapper() { _sizeAndBits |= WRAPPER_BIT; }
    /*
     * Mark the given allocation allocation as free.  This can be done after
     * the Directory has been resolved because sometimes traversal of various
     * data structures known to the finder may clarify the status, but at some
     * point all such changes have to stop, so that things that depend on the
     * free status, such as a Graph, can depend on those values not changing.
     */
    void MarkAsFree() {
      _sizeAndBits &= ~USED_BIT;  // Clear the bit that indicates use
    }

    /*
     * Mark the allocation as thread-cached.  This is done after the directory
     * has been resolved, because at that point the allocations have been
     * found, but before the graph is resolved for leak information because
     * a thread-cached allocation is considered free and so is neither
     * leaked or anchored.
     */
    void MarkAsThreadCached() {
      _sizeAndBits |= THREAD_CACHED_BIT;
      _sizeAndBits &= USED_BIT;
    }

    Offset Address() const { return _address; }
    Offset Size() const { return _sizeAndBits & SIZE_MASK; }
    bool IsUsed() const { return (_sizeAndBits & USED_BIT) != 0; }
    bool IsThreadCached() const {
      return (_sizeAndBits & THREAD_CACHED_BIT) != 0;
    }
    bool IsWrapper() const { return (_sizeAndBits & WRAPPER_BIT) != 0; }
    bool IsWrapped() const { return (_sizeAndBits & WRAPPED_BIT) != 0; }

    size_t FinderIndex() const {
      return (_sizeAndBits / LOW_FINDER_INDEX_BIT) % MAX_FINDERS;
    }

    // A Visitor returns true in the case that traversal should stop.
    typedef std::function<bool(Offset,        // allocation address
                               Offset,        // allocation size
                               bool,          // is allocated
                               const char*)>  // allocation image
        Visitor;
    // A Checker returns true in the case that the allocation should be
    // visited.
    typedef std::function<bool(Offset,        // allocation address
                               Offset,        // allocation size
                               bool,          // is allocated
                               const char*)>  // allocation image
        Checker;

   private:
    static constexpr Offset USED_BIT = ~((~((Offset)0)) >> 1);
    static constexpr Offset THREAD_CACHED_BIT = USED_BIT >> 1;
    static constexpr Offset WRAPPER_BIT = THREAD_CACHED_BIT >> 1;
    static constexpr Offset WRAPPED_BIT = WRAPPER_BIT >> 1;
    static constexpr Offset NUM_FINDER_INDEX_BITS =
        (sizeof(Offset) == 8) ? 8 : 2;
    static constexpr Offset LOW_FINDER_INDEX_BIT =
        WRAPPED_BIT >> NUM_FINDER_INDEX_BITS;
    static constexpr Offset SIZE_MASK = LOW_FINDER_INDEX_BIT - 1;

    Offset _address;
    Offset _sizeAndBits;

   public:
    static constexpr Offset MAX_FINDERS = 1 << NUM_FINDER_INDEX_BITS;
  };

  /*
   * This class is used to report a sequence of allocations just once, so
   * that information can be cached in a Directory.
   */

  class Finder {
   public:
    /*
     * Return true if there are no more allocations available.
     */
    virtual bool Finished() = 0;
    /*
     * Return the address of the next allocation (in increasing order of
     * address) to be reported by this finder, without advancing to the next
     * allocation.  The return value is undefined if there are no more
     * allocations available.  Note that at the time this function is called
     * any allocations already reported by this allocation finder have already
     * been assigned allocation indices in the Directory.
     */
    virtual Offset NextAddress() = 0;
    /*
     * Return the size of the next allocation (in increasing order of
     * address) to be reported by this finder, without advancing to the next
     * allocation.  The return value is undefined if there are no more
     * allocations available.
     */
    virtual Offset NextSize() = 0;
    /*
     * Return true if the next allocation (in increasing order of address) to
     * address) to be reported by this finder is considered used, without
     * advancing to the next allocation.
     */
    virtual bool NextIsUsed() = 0;
    /*
     * Advance to the next allocation.
     */
    virtual void Advance() = 0;
    /*
     * Return the smallest request size that might reasonably have resulted
     * in an allocation of the given size.
     */
    virtual Offset MinRequestSize(Offset size) = 0;
  };

  typedef std::function<void()> ResolutionDoneCallback;

  Directory()
      : _allocationBoundariesResolved(false),
        _freeStatusFinalized(false),
        _hasThreadCached(false),
        _maxAllocationSize(0) {}
  ~Directory() {}

  void AddFinder(Finder* finder) {
    if (_allocationBoundariesResolved) {
      /*
       * This can be done only before the allocation boundaries have been
       * resolved because no new allocations will be found after that.
       */
      abort();
    }
    if (_finderToIndex.find(finder) != _finderToIndex.end()) {
      /*
       * A finder can be added only once.
       */
      abort();
    }
    AllocationIndex numFinders = _indexToFinder.size();
    if (numFinders == Allocation::MAX_FINDERS) {
      /*
       * The maximum number of finders is hard-coded by the number of bits
       * used to keep the finder index in the Allocation.  At present that
       * number seems very generous but it could be raised.
       */
      abort();
    }

    _finderToIndex[finder] = _indexToFinder.size();
    _indexToFinder.push_back(finder);
  }

  void ResolveAllocationBoundaries() {
    if (_allocationBoundariesResolved) {
      abort();
    }
    std::vector<size_t> activeFinders;
    size_t numFinders = _indexToFinder.size();
    activeFinders.reserve(numFinders);
    for (size_t i = 0; i < numFinders; i++) {
      Finder* finder = _indexToFinder[i];
      if (!(finder->Finished())) {
        activeFinders.push_back(i);
      }
    }

    if (!activeFinders.empty()) {
      Offset numActiveFinders = activeFinders.size();
      if (numActiveFinders == 1) {
        AppendRemainingAllocationsFromFinder(activeFinders[0]);
      } else if (numActiveFinders == 2) {
        AppendRemainingAllocationsFromFinders(activeFinders[0],
                                              activeFinders[1]);
      } else {
        AppendRemainingAllocationsFromFinders(activeFinders);
      }
    }

    _allocationBoundariesResolved = true;
    for (auto& callback : _resolutionDoneCallbacks) {
      callback();
    }
  }

  bool AllocationBoundariesResolved() const {
    return _allocationBoundariesResolved;
  }

  void FinalizeFreeStatus() {
    if (!_allocationBoundariesResolved) {
      abort();
    }
    if (_freeStatusFinalized) {
      abort();
    }
    _freeStatusFinalized = true;
  }
  bool FreeStatusFinalized() const { return _freeStatusFinalized; }

  // index is same as NumAllocations() if offset is not in any range.
  AllocationIndex AllocationIndexOf(Offset addr) const {
    size_t limit = _allocations.size();
    size_t base = 0;
    while (base < limit) {
      size_t mid = (base + limit) / 2;
      const Allocation& allocation = _allocations[mid];
      Offset allocationAddress = allocation.Address();
      Offset allocationLimit = allocationAddress + allocation.Size();
      if (addr >= allocationAddress) {
        if (addr < allocationLimit && !allocation.IsWrapper()) {
          return (AllocationIndex)(mid);
        } else {
          base = mid + 1;
        }
      } else {
        limit = mid;
      }
    }
    for (const std::vector<AllocationIndex>& level : _wrappers) {
      /*
       * If there are any wrappers, the address might be in one of them but
       * not in any of the wrapped allocations it contains.
       * Search progressively outward.  The most common case is that there
       * are no wrappers at all.  The second most is that there are no wrappers
       * that wrap other wrappers, as can happen, for example, if python
       * allocates something using malloc() then further subdivides that thing
       * into allocations.
       */
      limit = level.size();
      base = 0;
      while (base < limit) {
        size_t mid = (base + limit) / 2;
        size_t allocationIndex = level[mid];
        const Allocation& allocation = _allocations[allocationIndex];
        Offset allocationAddress = allocation.Address();
        Offset allocationLimit = allocationAddress + allocation.Size();
        if (addr >= allocationAddress) {
          if (addr < allocationLimit) {
            return allocationIndex;
          } else {
            base = mid + 1;
          }
        } else {
          limit = mid;
        }
      }
    }
    return _allocations.size();
  }

  // null if index is not valid.
  const Allocation* AllocationAt(AllocationIndex index) const {
    if (index < _allocations.size()) {
      return &_allocations[index];
    }
    return (const Allocation*)0;
  }
  // 0 If index is not valid, otherwise somewhere <= the size of the
  // allocation.
  Offset MinRequestSize(AllocationIndex index) const {
    Offset minRequestSize = 0;
    if (index < _allocations.size()) {
      const Allocation& allocation = _allocations[index];
      minRequestSize = _indexToFinder[allocation.FinderIndex()]->MinRequestSize(
          allocation.Size());
    }
    return minRequestSize;
  }

  /*
   * Return the number of allocations.  This returns 0 before Resolve() has
   * been called.
   */
  AllocationIndex NumAllocations() const { return _allocations.size(); }

  /*
   * Return the maximum size of any allocation in the directory.  This returns
   * 0 before Resolve() has been called.
   */
  Offset MaxAllocationSize() const { return _maxAllocationSize; }

  /*
   * Mark the allocation at the given index as free or do nothing if the index
   * isn't valid.
   */
  void MarkAsFree(AllocationIndex index) {
    if (index < _allocations.size()) {
      _allocations[index].MarkAsFree();
    }
  }

  /*
   * Mark the allocation at the given index as being thread cached or do
   * nothing if the index isn't valid.  Note that marking an allocation as
   * thread cached also marks it s free.
   */
  void MarkAsThreadCached(AllocationIndex index) {
    if (index < _allocations.size()) {
      _allocations[index].MarkAsThreadCached();
      _hasThreadCached = true;
    }
  }

  /*
   * Return true if and only if the specified AllocationIndex is valid and
   * the corresponding allocation has been marked as thread-cached.
   */
  bool IsThreadCached(AllocationIndex index) const {
    return (index < _allocations.size()) &&
           _allocations[index].IsThreadCached();
  }

  /*
   * Return true if and only if at least one allocation is thread cached.
   */
  bool HasThreadCached() const { return _hasThreadCached; }

  /*
   * Add a callback to be invoked after all the allocation boundaries
   * have been resolved.
   */
  void AddResolutionDoneCallback(ResolutionDoneCallback cb) const {
    _resolutionDoneCallbacks.emplace_back(cb);
  }

 private:
  std::vector<Allocation> _allocations;
  bool _allocationBoundariesResolved;
  bool _freeStatusFinalized;
  bool _hasThreadCached;
  Offset _maxAllocationSize;
  std::map<Finder*, size_t> _finderToIndex;
  std::vector<Finder*> _indexToFinder;
  std::vector<std::pair<AllocationIndex, Offset> > _limits;
  std::vector<std::vector<AllocationIndex> > _wrappers;
  mutable std::vector<ResolutionDoneCallback> _resolutionDoneCallbacks;

  void ConsumeCurrentAllocation(size_t finderIndex, Finder* finder) {
    Offset address = finder->NextAddress();
    Offset size = finder->NextSize();
    Offset limit = address + size;
    bool isUsed = finder->NextIsUsed();
    bool isWrapped = false;
    finder->Advance();
    while (!_limits.empty() && limit > _limits.back().second) {
      if (address < _limits.back().second) {
        std::cerr << "Discarding allocation at [0x" << std::hex << address
                  << ", 0x" << limit
                  << ")\n... due to overlap with allocation at [0x" << std::hex
                  << _allocations[_limits.back().first].Address() << ", 0x"
                  << _limits.back().second << ")\n";
        return;
      }
      _limits.pop_back();
    }
    if (!_limits.empty()) {
      /*
       * This is a wrapped allocation, because another allocation contains
       * it.
       */
      isWrapped = true;
      AllocationIndex wrapperIndex = _limits.back().first;
      if (!(_allocations[wrapperIndex].IsWrapper())) {
        /*
         * The wrapping allocation was not previously known to be a wrapper.
         */
        _allocations[wrapperIndex].MarkAsWrapper();
        /*
         * Main the invariant that each wrapper is placed according to the
         * maximum level of nesting in that wrapper.  For example, _wrappers[0]
         * contains indices of wrappers that don't wrap any wrappers.
         */
        AllocationIndex toPlace = wrapperIndex;
        bool needNewLevel = true;
        for (std::vector<AllocationIndex>& level : _wrappers) {
          wrapperIndex = level.back();
          Allocation& allocation = _allocations[wrapperIndex];
          if (allocation.Size() + allocation.Address() < limit) {
            level.push_back(toPlace);
            needNewLevel = false;
            break;
          }
          level.back() = toPlace;
          toPlace = wrapperIndex;
        }
        if (needNewLevel) {
          _wrappers.emplace_back(std::vector<AllocationIndex>());
          _wrappers.back().push_back(toPlace);
        }
      }
    }
    _limits.emplace_back(_allocations.size(), limit);
    _allocations.emplace_back(address, size, isUsed, finderIndex, isWrapped);
    if (_maxAllocationSize < size) {
      _maxAllocationSize = size;
    }
  }
  void AppendRemainingAllocationsFromFinder(size_t finderIndex) {
    Finder* finder = _indexToFinder[finderIndex];
    while (!(finder->Finished())) {
      ConsumeCurrentAllocation(finderIndex, finder);
    }
  }
  void AppendRemainingAllocationsFromFinders(size_t finderIndex0,
                                             size_t finderIndex1) {
    Finder* finder0 = _indexToFinder[finderIndex0];
    Offset address0 = finder0->NextAddress();
    Offset size0 = finder0->NextSize();

    Finder* finder1 = _indexToFinder[finderIndex1];
    Offset address1 = finder1->NextAddress();
    Offset size1 = finder1->NextSize();

    while (true) {
      if (address0 < address1 || (address0 == address1 && size0 > size1)) {
        ConsumeCurrentAllocation(finderIndex0, finder0);
        if (finder0->Finished()) {
          AppendRemainingAllocationsFromFinder(finderIndex1);
          return;
        }
        address0 = finder0->NextAddress();
        size0 = finder0->NextSize();
      } else {
        ConsumeCurrentAllocation(finderIndex1, finder1);
        if (finder1->Finished()) {
          AppendRemainingAllocationsFromFinder(finderIndex0);
          return;
        }
        address1 = finder1->NextAddress();
        size1 = finder1->NextSize();
      }
    }
  }

  void Place(Offset address, Offset size, size_t finderIndex,
             std::vector<size_t>& activeFinders, size_t heapIndex) {
    Offset heapSize = activeFinders.size();
    Offset leftChild = 2 * heapIndex + 1;
    Offset rightChild = leftChild + 1;
    while (rightChild < heapSize) {
      size_t leftFinderIndex = activeFinders[leftChild];
      Finder* leftFinder = _indexToFinder[leftFinderIndex];
      Offset leftAddress = leftFinder->NextAddress();
      Offset leftSize = leftFinder->NextSize();
      size_t rightFinderIndex = activeFinders[rightChild];
      Finder* rightFinder = _indexToFinder[rightFinderIndex];
      Offset rightAddress = rightFinder->NextAddress();
      Offset rightSize = rightFinder->NextSize();
      if (leftAddress < rightAddress ||
          (leftAddress == rightAddress && leftSize > rightSize)) {
        if (address < leftAddress ||
            (address == leftAddress && address >= leftSize)) {
          activeFinders[heapIndex] = finderIndex;
          return;
        }
        activeFinders[heapIndex] = leftFinderIndex;
        heapIndex = leftChild;
      } else {
        if (address < rightAddress ||
            (address == rightAddress && address >= rightSize)) {
          activeFinders[heapIndex] = finderIndex;
          return;
        }
        activeFinders[heapIndex] = rightFinderIndex;
        heapIndex = rightChild;
      }
    }
    if (leftChild < heapSize) {
      size_t leftFinderIndex = activeFinders[leftChild];
      Finder* leftFinder = _indexToFinder[leftFinderIndex];
      Offset leftAddress = leftFinder->NextAddress();
      if ((address < leftAddress ||
           (address == leftAddress && size > leftFinder->NextSize()))) {
        activeFinders[heapIndex] = finderIndex;
      } else {
        activeFinders[heapIndex] = leftFinderIndex;
        activeFinders[leftChild] = finderIndex;
      }
    } else {
      activeFinders[heapIndex] = finderIndex;
    }
  }

  void AppendRemainingAllocationsFromFinders(
      std::vector<size_t>& activeFinders) {
    size_t numActiveFinders = activeFinders.size();
    std::make_heap(activeFinders.begin(), activeFinders.end(),
                   [this](const size_t left, const size_t right) {
                     Finder* leftFinder = this->_indexToFinder[left];
                     Finder* rightFinder = this->_indexToFinder[right];
                     Offset leftAddress = leftFinder->NextAddress();
                     Offset rightAddress = rightFinder->NextAddress();
                     return (leftAddress > rightAddress) ||
                            ((leftAddress == rightAddress) &&
                             leftFinder->NextSize() < rightFinder->NextSize());
                   });
    size_t topFinderIndex = activeFinders[0];
    Finder* topFinder = _indexToFinder[topFinderIndex];
    Offset topAddress = topFinder->NextAddress();
    Offset topSize = topFinder->NextSize();

    Finder* leftFinder = _indexToFinder[activeFinders[1]];
    Offset leftAddress = leftFinder->NextAddress();
    Offset leftSize = leftFinder->NextSize();

    Finder* rightFinder = _indexToFinder[activeFinders[2]];
    Offset rightAddress = rightFinder->NextAddress();
    Offset rightSize = rightFinder->NextSize();

    bool leftIsNext = (leftAddress < rightAddress) ||
                      (leftAddress == rightAddress && leftSize >= rightSize);
    Offset nextAddress = leftIsNext ? leftAddress : rightAddress;
    Offset nextSize = leftIsNext ? leftSize : rightSize;

    while (true) {
      ConsumeCurrentAllocation(topFinderIndex, topFinder);
      if (topFinder->Finished()) {
        size_t lastFinderIndex = activeFinders[numActiveFinders - 1];
        activeFinders.pop_back();
        if (--numActiveFinders == 2) {
          AppendRemainingAllocationsFromFinders(lastFinderIndex,
                                                activeFinders[1]);
          return;
        }
        activeFinders[0] = lastFinderIndex;
        topFinderIndex = lastFinderIndex;
        topFinder = _indexToFinder[topFinderIndex];
      }
      topAddress = topFinder->NextAddress();
      topSize = topFinder->NextSize();
      if (nextAddress < topAddress ||
          (nextAddress == topAddress && nextSize > topSize)) {
        /*
         * The top finder is no longer in the correct place.  This is expected
         * not to happen all that often because many consecutive allocations
         * will be from the same finder.
         */
        Offset address = topAddress;
        Offset size = topSize;
        Offset finderIndex = topFinderIndex;
        topAddress = nextAddress;
        topSize = nextSize;
        if (leftIsNext) {
          topFinderIndex = activeFinders[1];
          activeFinders[0] = topFinderIndex;
          topFinder = leftFinder;
          Place(address, size, finderIndex, activeFinders, 1);
          leftFinder = _indexToFinder[activeFinders[1]];
          leftAddress = leftFinder->NextAddress();
          leftSize = leftFinder->NextSize();
        } else {
          topFinderIndex = activeFinders[2];
          activeFinders[0] = topFinderIndex;
          topFinder = rightFinder;
          Place(address, size, finderIndex, activeFinders, 2);
          rightFinder = _indexToFinder[activeFinders[2]];
          rightAddress = rightFinder->NextAddress();
          rightSize = rightFinder->NextSize();
        }
        leftIsNext = (leftAddress < rightAddress) ||
                     (leftAddress == rightAddress && leftSize >= rightSize);
        nextAddress = leftIsNext ? leftAddress : rightAddress;
        nextSize = leftIsNext ? leftSize : rightSize;
      }
    }
  }
};
}  // namespace Allocations
}  // namespace chap
