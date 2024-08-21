// Copyright (c) 2020,2023-2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <algorithm>
#include "../ModuleDirectory.h"
#include "../StackRegistry.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"
#include "MappedPageRangeIterator.h"

namespace chap {
namespace GoLang {
template <typename Offset>
class InfrastructureFinder {
 public:
  static constexpr Offset NOT_A_FIELD_OFFSET = ~((Offset)0);
  static constexpr Offset NOT_A_MAPPED_ADDRESS = ~((Offset)0);
  static constexpr Offset STACK_BASE_IN_GOROUTINE = 0;
  static constexpr Offset STACK_LIMIT_IN_GOROUTINE =
      STACK_BASE_IN_GOROUTINE + sizeof(Offset);
  static constexpr Offset STACK_GUARD0_IN_GOROUTINE =
      STACK_LIMIT_IN_GOROUTINE + sizeof(Offset);
  static constexpr Offset STACK_GUARD1_IN_GOROUTINE =
      STACK_GUARD0_IN_GOROUTINE + sizeof(Offset);
  static constexpr Offset STACK_POINTER_IN_GOROUTINE = 7 * sizeof(Offset);
  static constexpr Offset PAGE_SHIFT = 13;
  static constexpr Offset PAGE_SIZE = 1 << PAGE_SHIFT;
  static constexpr Offset SPANS_INDEX_SHIFT = 13;
  static constexpr Offset SPANS_ARRAY_SIZE = sizeof(Offset)
                                             << SPANS_INDEX_SHIFT;
  static constexpr Offset ARENA_SIZE = 1 << (SPANS_INDEX_SHIFT + PAGE_SHIFT);
  static constexpr Offset ARENAS_INDEX_BITS = 21;
  static constexpr Offset ARENAS_ARRAY_SIZE = sizeof(Offset)
                                              << ARENAS_INDEX_BITS;
  static constexpr Offset ARENAS_ARRAY_REL_ARENAS_FIELD_VALUE =
      ARENAS_ARRAY_SIZE;

  static constexpr Offset SPANS_ARRAY_IN_HEAP_ARENA = 0x200000;
  static constexpr Offset NUM_SPANS_UPPER_BOUND =
      ((~((Offset)0)) >> PAGE_SHIFT) + 1;
  static constexpr Offset NUM_PAGES_UPPER_BOUND = NUM_SPANS_UPPER_BOUND;
  static constexpr uint8_t SPAN_STATE_DEAD = 0;
  static constexpr uint8_t SPAN_STATE_IN_USE = 1;
  static constexpr uint8_t SPAN_STATE_MANUAL = 2;
  static constexpr uint8_t SPAN_STATE_FREE = 3;
  static constexpr uint8_t MAX_SPAN_STATE = SPAN_STATE_FREE;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  InfrastructureFinder(const ModuleDirectory<Offset>& moduleDirectory,
                       VirtualMemoryPartition<Offset>& partition,
                       StackRegistry<Offset>& stackRegistry)
      : GOLANG_MAPPED_PAGES("golang mapped pages"),
        GOLANG_SPAN("golang span"),
        GOROUTINE_STACK("goroutine stack"),
        _moduleDirectory(moduleDirectory),
        _isResolved(false),
        _virtualMemoryPartition(partition),
        _stackRegistry(stackRegistry),
        _virtualAddressMap(partition.GetAddressMap()),
        _arenasFieldValue(0),
        _spansInHeapArena(NOT_A_FIELD_OFFSET),
        _pageOffsetBits(0),
        _pageSize(0),
        _startAddrInMspan(NOT_A_FIELD_OFFSET),
        _numPagesInMspan(NOT_A_FIELD_OFFSET),
        _limitInMspan(NOT_A_FIELD_OFFSET),
        _numElementsInMspan(NOT_A_FIELD_OFFSET),
        _allocBitsInMspan(NOT_A_FIELD_OFFSET),
        _manualFreeListInMspan(NOT_A_FIELD_OFFSET),
        _elementSizeInMspan(NOT_A_FIELD_OFFSET),
        _stateInMspan(NOT_A_FIELD_OFFSET),
        _sizes(0),
        _numSizes(0),
        _mspanSize(NOT_A_FIELD_OFFSET),
        _goRoutineSize(NOT_A_FIELD_OFFSET) {}

  const char* GOLANG_MAPPED_PAGES;
  const char* GOLANG_SPAN;
  const char* GOROUTINE_STACK;

  void Resolve() {
    if (_isResolved) {
      abort();
    }
    if (!_moduleDirectory.IsResolved()) {
      abort();
    }

    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      if (nameAndModuleInfo.first.find(".so") != std::string::npos) {
        // For now, assume the go runtime code is not in a shared library.
        continue;
      }
      const auto& moduleInfo = nameAndModuleInfo.second;

      /*
       * At this point, we have the header to the goroutines.
       */

      if (FindArenasField(moduleInfo)) {
        std::cerr << "Warning: This is a core for a GoLang process.\n"
                     "... Some GoLang allocations are not found yet.\n"
                     "... Leak analysis is not accurate, in part due to "
                     "garbage collection.\n";
        FindPageLimitsAndRegisterMappedPages();
        DeriveRemainingMspanOffsets();
        RegisterGoRoutineStacks();
      }
    }
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }
  bool FoundRangesAndSizes() const {
    return _arenasFieldValue != 0 && _sizes != 0;
  }
  Offset GetArenasFieldValue() const { return _arenasFieldValue; }
  Offset GetNumElementsInMspan() const { return _numElementsInMspan; }
  Offset GetAllocBitsInMspan() const { return _allocBitsInMspan; }
  Offset GetManualFreeListInMspan() const { return _manualFreeListInMspan; }
  Offset GetElementSizeInMspan() const { return _elementSizeInMspan; }
  Offset GetStateInMspan() const { return _stateInMspan; }
  Offset GetPageOffsetBits() const { return _pageOffsetBits; }
  Offset GetSizes() const { return _sizes; }
  Offset GetNumSizes() const { return _numSizes; }
  MappedPageRangeIterator<Offset>* MakeMappedPageRangeIterator() const {
    return new MappedPageRangeIterator<Offset>(
        _virtualAddressMap, _arenasFieldValue, _spansInHeapArena,
        _arenasIndexBits, _spansIndexBits, _pageOffsetBits, _startAddrInMspan,
        _numPagesInMspan, _firstMappedPage, _lastMappedPage);
  }

  bool IsPlausibleGoRoutine(Reader& reader, Offset goRoutine) const {
    if (reader.ReadOffset(goRoutine + 9 * sizeof(Offset), 0xbad) != goRoutine) {
      return false;
    }
    Offset stackBase =
        reader.ReadOffset(goRoutine + STACK_BASE_IN_GOROUTINE, 0xbad);
    if ((stackBase & 0x3f) != 0) {
      return false;
    }
    Offset stackLimit =
        reader.ReadOffset(goRoutine + STACK_LIMIT_IN_GOROUTINE, 0xbad);
    if ((stackBase & 0x3f) != 0) {
      return false;
    }
    if (stackBase == 0) {
      if (stackLimit != 0) {
        return false;
      }
    } else {
      if (stackLimit <= stackBase) {
        return false;
      }
      Offset stackGuard0 =
          reader.ReadOffset(goRoutine + STACK_GUARD0_IN_GOROUTINE, 0);
      if (((stackGuard0 < stackBase) || (stackGuard0 > stackLimit)) &&
          (stackGuard0 != ~((Offset)(0x521)))) {
        return false;
      }
      Offset stackGuard1 =
          reader.ReadOffset(goRoutine + STACK_GUARD1_IN_GOROUTINE, 0);
      if (((stackGuard1 < stackBase) || (stackGuard1 > stackLimit)) &&
          (stackGuard1 != ~((Offset)(0x0)))) {
        return false;
      }
    }
    return true;
  }

 private:
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  bool _isResolved;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  StackRegistry<Offset>& _stackRegistry;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  Offset _arenasFieldValue;
  Offset _spansInHeapArena;
  Offset _arenasIndexBits;
  Offset _spansIndexBits;
  Offset _pageOffsetBits;
  Offset _pageSize;
  Offset _startAddrInMspan;
  Offset _numPagesInMspan;
  Offset _limitInMspan;
  Offset _numElementsInMspan;
  Offset _allocBitsInMspan;
  Offset _manualFreeListInMspan;
  Offset _elementSizeInMspan;
  Offset _stateInMspan;
  Offset _sizes;
  Offset _numSizes;
  Offset _firstMappedPage;
  Offset _lastMappedPage;
  Offset _mspanSize;
  Offset _goRoutineSize;

  bool DeriveMspanSize(Reader& spanReader) {
    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    for (iterator.reset(MakeMappedPageRangeIterator()); !(iterator->Finished());
         iterator->Advance()) {
      Offset mspan = iterator->Mspan();
      if (mspan == 0) {
        continue;
      }
      for (Offset mspanSize = 0x80; mspanSize < 0x400; mspanSize += 8) {
        Offset numPages;
        Offset firstAddressForSpan;
        if (IsPlausibleMspan(spanReader, mspan + mspanSize, numPages,
                             firstAddressForSpan) ||
            IsPlausibleMspan(spanReader, mspan - mspanSize, numPages,
                             firstAddressForSpan)) {
          _mspanSize = mspanSize;
          return true;
        }
      }
    }

    std::cerr << "Error: cannot derive the mspan size.\n";
    return false;
  }

  bool DeriveStateInMspan(Reader& spanReader, const std::vector<bool>& u8Used) {
    std::vector<bool> ruledOut(u8Used);
    size_t numCandidates = u8Used.size();
    std::vector<std::pair<Offset, Offset> > count1And2;
    std::pair<Offset, Offset> initCounts(0, 0);
    count1And2.resize(numCandidates, initCounts);
    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    for (iterator.reset(MakeMappedPageRangeIterator()); !(iterator->Finished());
         iterator->Advance()) {
      Offset mspan = iterator->Mspan();
      if (mspan == 0) {
        continue;
      }
      bool haveCandidateLeft = false;
      for (size_t i = 0; i < numCandidates; i++) {
        if (ruledOut[i]) {
          continue;
        }
        unsigned char stateCandidate = spanReader.ReadU8(mspan + i, ~0);
        if (stateCandidate > 2) {
          ruledOut[i] = true;
          continue;
        }
        haveCandidateLeft = true;
        if (stateCandidate == 1) {
          count1And2[i].first++;
        } else if (stateCandidate == 2) {
          count1And2[i].second++;
        }
      }
      if (!haveCandidateLeft) {
        std::cerr << "Error: failed to derive state field in mspan.\n";
        return false;
      }
    }
    Offset bestCandidate = numCandidates;
    Offset bestTotal1And2 = 0;
    for (Offset i = 0; i < numCandidates; i++) {
      if (ruledOut[i]) {
        continue;
      }
      const auto& counts = count1And2[i];
      if ((counts.first == 0) || (counts.second == 0)) {
        continue;
      }
      Offset total1And2 = counts.first + counts.second;
      if (total1And2 > bestTotal1And2) {
        bestTotal1And2 = total1And2;
        bestCandidate = i;
      }
    }
    if (bestCandidate == numCandidates) {
      std::cerr << "Error: failed to derive state field in mspan.\n";
      return false;
    }
    _stateInMspan = bestCandidate;
    return true;
  }

  bool DeriveElementSizeInMspan(Reader& spanReader,
                                const std::vector<bool>& uintptrUsed) {
    size_t numCandidates = uintptrUsed.size();
    std::vector<Offset> votes;
    votes.resize(numCandidates, 0);
    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    for (iterator.reset(MakeMappedPageRangeIterator()); !(iterator->Finished());
         iterator->Advance()) {
      Offset mspan = iterator->Mspan();
      if (mspan == 0) {
        continue;
      }
      unsigned char state = spanReader.ReadU8(mspan + _stateInMspan, 0);
      if (state != 1) {
        // Although element size can be set in other states, this one is
        // probably the simplest to analyze.
        continue;
      }

      Offset startAddress = iterator->FirstAddressForRange();
      Offset limit = spanReader.ReadOffset(mspan + _limitInMspan, 0);
      if (limit < startAddress) {
        continue;
      }
      Offset bytesToLimit = limit - startAddress;
      for (size_t i = 0; i < numCandidates; i++) {
        if (uintptrUsed[i]) {
          continue;
        }
        Offset elementSize =
            spanReader.ReadOffset(mspan + i * sizeof(Offset), 1);

        if ((elementSize != 0) && ((elementSize & (sizeof(Offset) - 1)) == 0) &&
            (bytesToLimit % elementSize == 0)) {
          votes[i]++;
        }
      }
    }
    Offset bestCandidate = numCandidates;
    Offset bestVotes = 0;
    for (Offset i = 0; i < numCandidates; i++) {
      Offset numVotes = votes[i];
      if (bestVotes < numVotes) {
        bestVotes = numVotes;
        bestCandidate = i;
      }
    }
    if (bestCandidate == numCandidates) {
      std::cerr
          << "Error: failed to derive element size field offset in mspan.\n";
      return false;
    }
    _elementSizeInMspan = bestCandidate * sizeof(Offset);
    return true;
  }
  bool DeriveNumElementsInMspan(Reader& spanReader,
                                const std::vector<bool>& u16Used) {
    size_t numCandidates = u16Used.size();
    std::vector<Offset> votes;
    votes.resize(numCandidates, 0);
    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    for (iterator.reset(MakeMappedPageRangeIterator()); !(iterator->Finished());
         iterator->Advance()) {
      Offset mspan = iterator->Mspan();
      if (mspan == 0) {
        continue;
      }
      unsigned char state = spanReader.ReadU8(mspan + _stateInMspan, 0);
      if (state != 1) {
        // Although element size can be set in other states, this one is
        // probably the simplest to analyze.
        continue;
      }

      Offset startAddress = iterator->FirstAddressForRange();
      Offset limit = spanReader.ReadOffset(mspan + _limitInMspan, 0);
      if (limit < startAddress) {
        continue;
      }
      Offset bytesToLimit = limit - startAddress;
      Offset elementSize =
          spanReader.ReadOffset(mspan + _elementSizeInMspan, 0);

      if (elementSize == 0) {
        continue;
      }

      Offset rangeSize = iterator->RangeSize();
      Offset experimentalUsable = rangeSize - (rangeSize >> 6);
      for (size_t i = 0; i < numCandidates; i++) {
        if (u16Used[i]) {
          continue;
        }
        Offset numElements = spanReader.ReadU16(mspan + i * 2, 0);
        if ((numElements != 0) &&
            ((numElements * elementSize == bytesToLimit) ||
             (experimentalUsable / elementSize == numElements))) {
          votes[i]++;
        }
      }
    }
    Offset bestCandidate = numCandidates;
    Offset bestVotes = 0;
    for (Offset i = 0; i < numCandidates; i++) {
      Offset numVotes = votes[i];
      if (bestVotes < numVotes) {
        bestVotes = numVotes;
        bestCandidate = i;
      }
    }
    if (bestCandidate == numCandidates) {
      std::cerr
          << "Error: failed to derive element size field offset in mspan.\n";
      return false;
    }
    _numElementsInMspan = bestCandidate * 2;
    return true;
  }

  bool DeriveAllocBitsInMspan(Reader& spanReader,
                              const std::vector<bool>& uintptrUsed) {
    Reader allocBitsReader(_virtualAddressMap);

    size_t numCandidates = uintptrUsed.size();
    std::vector<Offset> votes;
    votes.resize(numCandidates, 0);
    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    for (iterator.reset(MakeMappedPageRangeIterator()); !(iterator->Finished());
         iterator->Advance()) {
      Offset mspan = iterator->Mspan();
      if (mspan == 0) {
        continue;
      }
      unsigned char state = spanReader.ReadU8(mspan + _stateInMspan, 0);
      if (state != 1) {
        // Although element size can be set in other states, this one is
        // probably the simplest to analyze.
        continue;
      }

      unsigned short numElements =
          spanReader.ReadU16(mspan + _numElementsInMspan, 0);

      if (numElements > (sizeof(Offset) * 8 / 2)) {
        // allocBits does apply in other cases, but these should suffice here.
        continue;
      }
      Offset bitsLimit = 1 << numElements;

      for (size_t i = 0; i < numCandidates; i++) {
        if (uintptrUsed[i]) {
          continue;
        }
        Offset allocBits = spanReader.ReadOffset(mspan + i * sizeof(Offset), 0);
        if (allocBits == 0) {
          continue;
        }
        Offset firstBits = allocBitsReader.ReadOffset(allocBits, 0);
        if (firstBits > 0 && firstBits < bitsLimit) {
          votes[i]++;
        }
      }
    }
    Offset bestCandidate = numCandidates;
    Offset bestVotes = 0;
    for (Offset i = 0; i < numCandidates; i++) {
      Offset numVotes = votes[i];
      if (bestVotes < numVotes) {
        bestVotes = numVotes;
        bestCandidate = i;
      }
    }
    if (bestCandidate == numCandidates) {
      std::cerr << "Error: failed to derive allocBits field offset in mspan.\n";
      return false;
    }
    _allocBitsInMspan = bestCandidate * sizeof(Offset);
    return true;
  }

  bool DeriveManualFreeListInMspan(Reader& spanReader,
                                   const std::vector<bool>& uintptrUsed) {
    Reader allocBitsReader(_virtualAddressMap);

    size_t numCandidates = uintptrUsed.size();
    std::vector<Offset> votes;
    votes.resize(numCandidates, 0);
    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    for (iterator.reset(MakeMappedPageRangeIterator()); !(iterator->Finished());
         iterator->Advance()) {
      Offset mspan = iterator->Mspan();
      if (mspan == 0) {
        continue;
      }
      unsigned char state = spanReader.ReadU8(mspan + _stateInMspan, 0);
      if (state != 2) {
        // Although element size can be set in other states, this one is
        // probably the simplest to analyze.
        continue;
      }

      Offset startAddress = iterator->FirstAddressForRange();
      Offset limit = startAddress + iterator->RangeSize();

      for (size_t i = 0; i < numCandidates; i++) {
        if (uintptrUsed[i]) {
          continue;
        }
        Offset firstFree = spanReader.ReadOffset(mspan + i * sizeof(Offset), 0);
        if ((firstFree >= startAddress) && (firstFree < limit)) {
          votes[i]++;
        }
      }
    }
    Offset bestCandidate = numCandidates;
    Offset bestVotes = 0;
    for (Offset i = 0; i < numCandidates; i++) {
      Offset numVotes = votes[i];
      if (bestVotes < numVotes) {
        bestVotes = numVotes;
        bestCandidate = i;
      }
    }
    if (bestCandidate == numCandidates) {
      /*
       * There is a possibility that we won't find this, if there
       * are simply no non-empty manual free lists, so don't consider
       * failure to find this field fatal.
       */
      return false;
    }
    _manualFreeListInMspan = bestCandidate * sizeof(Offset);
    return true;
  }

  void MarkUsed(const Offset fieldOffset, const Offset fieldSize,
                std::vector<bool>& u8Used, std::vector<bool>& u16Used,
                std::vector<bool>& uintptrUsed) {
    Offset fieldLimit = fieldOffset + fieldSize;
    for (Offset o = fieldOffset; o < fieldLimit; o++) {
      u8Used[o] = true;
    }
    for (Offset o = fieldOffset; o < fieldLimit; o += 2) {
      u16Used[o / 2] = true;
    }
    for (Offset o = fieldOffset; o < fieldLimit; o += sizeof(Offset)) {
      uintptrUsed[o / sizeof(Offset)] = true;
    }
  }

  bool DeriveRemainingMspanOffsets() {
    Reader spanReader(_virtualAddressMap);
    if (!DeriveMspanSize(spanReader)) {
      return false;
    }
    std::vector<bool> u8Used;
    std::vector<bool> u16Used;
    std::vector<bool> uintptrUsed;
    u8Used.resize(_mspanSize, false);
    u16Used.resize(_mspanSize / 2, false);
    uintptrUsed.resize(_mspanSize / sizeof(Offset), false);
    MarkUsed(_startAddrInMspan, sizeof(Offset), u8Used, u16Used, uintptrUsed);
    MarkUsed(_numPagesInMspan, sizeof(Offset), u8Used, u16Used, uintptrUsed);
    MarkUsed(_limitInMspan, sizeof(Offset), u8Used, u16Used, uintptrUsed);
    if (!DeriveStateInMspan(spanReader, u8Used)) {
      return false;
    }
    MarkUsed(_stateInMspan, 1, u8Used, u16Used, uintptrUsed);
    if (!DeriveElementSizeInMspan(spanReader, uintptrUsed)) {
      return false;
    }
    MarkUsed(_elementSizeInMspan, sizeof(Offset), u8Used, u16Used, uintptrUsed);
    if (!DeriveNumElementsInMspan(spanReader, u16Used)) {
      return false;
    }
    MarkUsed(_numElementsInMspan, 2, u8Used, u16Used, uintptrUsed);
    if (!DeriveAllocBitsInMspan(spanReader, uintptrUsed)) {
      return false;
    }
    MarkUsed(_allocBitsInMspan, sizeof(Offset), u8Used, u16Used, uintptrUsed);
    if (DeriveManualFreeListInMspan(spanReader, uintptrUsed)) {
      /*
       * This won't get derived if there are no non-empty manual free
       * lists, but this is not a problem because there will then be
       * none to traverse and we won't need to know where the heads are.
       */
      MarkUsed(_manualFreeListInMspan, sizeof(Offset), u8Used, u16Used,
               uintptrUsed);
    }
    return true;
  }

  bool FindSizes(Offset start, Offset limit, Reader& reader) {
    size_t sequenceLength = 0;
    uint16_t lastSize = 0;
    Offset check;
    for (check = start; limit; check += 2) {
      uint16_t size = reader.ReadU16(check, 0xbad);
      if (size == 0) {
        if (sequenceLength > 60) {
          break;
        }
        sequenceLength = 1;
        lastSize = 0;
        continue;
      }
      if (sequenceLength == 0) {
        continue;
      }
      if (((size & (sizeof(Offset) - 1)) != 0) || (size <= lastSize)) {
        if (sequenceLength > 60) {
          break;
        }
        sequenceLength = 0;
        continue;
      }
      lastSize = size;
      sequenceLength++;
    }
    if (sequenceLength > 60) {
      _sizes = check - sequenceLength * 2;
      _numSizes = sequenceLength;
      return true;
    }
    return false;
  }

  bool RegisterGoRoutineStacks() {
    Reader mspanReader(_virtualAddressMap);
    Reader allocBitsReader(_virtualAddressMap);
    Reader goRoutineReader(_virtualAddressMap);

    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    for (iterator.reset(MakeMappedPageRangeIterator()); !(iterator->Finished());
         iterator->Advance()) {
      Offset mspan = iterator->Mspan();
      if (mspan == 0) {
        continue;
      }
      unsigned char state = mspanReader.ReadU8(mspan + _stateInMspan, 0);
      if (state != 1) {
        // Although element size can be set in other states, this one is
        // probably the simplest to analyze.
        continue;
      }

      Offset elementSize =
          mspanReader.ReadOffset(mspan + _elementSizeInMspan, 0);
      if (_goRoutineSize != NOT_A_FIELD_OFFSET) {
        if (elementSize != _goRoutineSize) {
          continue;
        }
      } else {
        if (elementSize < 0x180 || elementSize > 0x200) {
          continue;
        }
      }

      Offset firstAddress = iterator->FirstAddressForRange();
      Offset allocBits = mspanReader.ReadOffset(mspan + _allocBitsInMspan, 0);
      unsigned short numElements =
          mspanReader.ReadU16(mspan + _numElementsInMspan, 0);
      unsigned char bits = 0;
      for (size_t i = 0; i < numElements; i++) {
        if ((i & 7) == 0) {
          bits = allocBitsReader.ReadU8(allocBits + (i / 8), 0);
        }
        if ((bits & (1 << (i & 7))) == 0) {
          continue;
        }
        Offset goRoutine = firstAddress + i * elementSize;
        if (!IsPlausibleGoRoutine(goRoutineReader, goRoutine)) {
          continue;
        }
        Offset stackBase = goRoutineReader.ReadOffset(
            goRoutine + STACK_BASE_IN_GOROUTINE, 0xbad);
        if ((stackBase == 0) || ((stackBase & 0x3f) != 0)) {
          continue;
        }
        Offset stackLimit = goRoutineReader.ReadOffset(
            goRoutine + STACK_LIMIT_IN_GOROUTINE, 0xbad);
        if ((stackLimit < stackBase) || ((stackLimit & 0x3f) != 0)) {
          continue;
        }
        if (!_stackRegistry.RegisterStack(stackBase, stackLimit,
                                          GOROUTINE_STACK)) {
          std::cerr << "Failed to register stack at [0x" << std::hex
                    << stackBase << ", 0x" << stackLimit
                    << ") due to overlap with other stack.\n";
          continue;
        }
        /*
         * Note: This may not be the most current stack pointer for a
         * thread that was running at the time of the core, but if not
         * it will be fixed at the time the thread is associated with
         * the goroutine.
         */
        Offset stackPointer = goRoutineReader.ReadOffset(
            goRoutine + STACK_POINTER_IN_GOROUTINE, 0xbad);
        if (stackPointer >= stackBase && stackPointer < stackLimit &&
            !_stackRegistry.AddStackTop(stackPointer)) {
          std::cerr << "Warning: Failed to set saved stack pointer for "
                       "goroutine at 0x"
                    << std::hex << goRoutine << "\n";
        }
        _goRoutineSize = elementSize;
      }
    }
    if (_goRoutineSize == NOT_A_FIELD_OFFSET) {
      std::cerr << "Error: failed to find any goroutine stacks.\n";
      return false;
    }
    return true;
  }

  void ClearValuesDerivedFromSingleSpan() {
    _pageOffsetBits = 0;
    _pageSize = 0;
    _startAddrInMspan = NOT_A_FIELD_OFFSET;
    _numPagesInMspan = NOT_A_FIELD_OFFSET;
    _limitInMspan = NOT_A_FIELD_OFFSET;
  }

  bool DeriveValuesFromSingleSpan(Reader& spanReader, Offset spanCandidate) {
    for (Offset startAddrInMspan = 0; startAddrInMspan < (8 * sizeof(Offset));
         startAddrInMspan += sizeof(Offset)) {
      Offset startAddr =
          spanReader.ReadOffset(spanCandidate + startAddrInMspan, 1);
      if (startAddr == 0) {
        continue;
      }
      if ((startAddr & 0xfff) != 0) {
        continue;
      }
      // Assume start address is just before page count
      Offset numPagesInMspan = startAddrInMspan + sizeof(Offset);
      Offset numPages =
          spanReader.ReadOffset(spanCandidate + numPagesInMspan, 1);

      if (numPages == 0) {
        continue;
      }

      for (Offset limitInMspan = startAddrInMspan + 4 * sizeof(Offset);
           limitInMspan < 0x10 * sizeof(Offset);
           limitInMspan += sizeof(Offset)) {
        Offset limit = spanReader.ReadOffset(spanCandidate + limitInMspan, 1);
        if (limit <= startAddr) {
          continue;
        }
        Offset pageOffsetBits = 12;
        for (; pageOffsetBits < 19; pageOffsetBits++) {
          if ((startAddr + (numPages << pageOffsetBits) >= limit)) {
            break;
          }
        }
        if (pageOffsetBits == 19) {
          continue;
        }
        if ((numPages > 1) &&
            (limit <= startAddr + ((numPages - 1) << pageOffsetBits))) {
          continue;
        }
        _pageOffsetBits = pageOffsetBits;
        _pageSize = 1 << pageOffsetBits;
        _startAddrInMspan = startAddrInMspan;
        _numPagesInMspan = numPagesInMspan;
        _limitInMspan = limitInMspan;
        return true;
      }
    }
    return false;
  }

  bool IsPlausibleMspan(Reader& spanReader, Offset candidate, Offset& numPages,
                        Offset& firstAddressForSpan) {
    if (_startAddrInMspan == NOT_A_FIELD_OFFSET) {
      if (!DeriveValuesFromSingleSpan(spanReader, candidate)) {
        return false;
      }
      _arenasIndexBits = ARENAS_INDEX_BITS;
      _spansIndexBits = 26 - _pageOffsetBits;
    }
    firstAddressForSpan =
        spanReader.ReadOffset(candidate + _startAddrInMspan, 0);
    if (firstAddressForSpan == 0) {
      return false;
    }
    if ((firstAddressForSpan & (_pageSize - 1)) != 0) {
      return false;
    }
    numPages = spanReader.ReadOffset(candidate + _numPagesInMspan, 0);
    if (numPages == 0) {
      return false;
    }
    Offset limitForSpan = firstAddressForSpan + (numPages << _pageOffsetBits);
    if (limitForSpan <= firstAddressForSpan) {
      return false;
    }
    Offset allocationsLimit =
        spanReader.ReadOffset(candidate + _limitInMspan, 0);
    if (allocationsLimit > limitForSpan) {
      return false;
    }
    return true;
  }

  bool IsPlausibleHeapArena(Offset candidate, Offset baseAddress,
                            Offset& numSpansFound) {
    numSpansFound = 0;
    if (candidate == 0) {
      return true;
    }
    if ((candidate & 0xfff) != 0) {
      return false;
    }
    Reader arenaReader(_virtualAddressMap);
    Offset arrayStart = candidate + _spansInHeapArena;
    Offset arrayLimit = arrayStart + SPANS_ARRAY_SIZE;
    if (arrayLimit < candidate) {
      return false;
    }
    Offset checkArray = arrayStart;
    Offset indexInArray = 0;
    Reader spanReader(_virtualAddressMap);
    while (checkArray < arrayLimit) {
      Offset spanCandidate = arenaReader.ReadOffset(checkArray, 1);
      if (spanCandidate == 0) {
        checkArray += sizeof(Offset);
        indexInArray++;
        continue;
      }
      Offset numPages;
      Offset firstAddressForSpan;
      if (!IsPlausibleMspan(spanReader, spanCandidate, numPages,
                            firstAddressForSpan)) {
        return false;
      }
      if (firstAddressForSpan ==
          baseAddress + (indexInArray << _pageOffsetBits)) {
        numSpansFound++;
      } else {
        numPages = 1;
      }

      checkArray += (numPages * sizeof(Offset));
      indexInArray += numPages;
    }
    return true;
  }

  /*
   * TODO: This assumes that the heap arena pointers are all held in a
   * one dimensional array of fixed size.  This may need to change at
   * some point to support less common configurations.
   */

  bool IsPlausibleArenasFieldValue(Offset candidate, Reader& arenasReader) {
    if (candidate == 0) {
      return false;
    }
    if ((candidate & 0xfff) != 0) {
      return false;
    }
    Offset arrayStart = candidate + ARENAS_ARRAY_REL_ARENAS_FIELD_VALUE;
    Offset arrayLimit = arrayStart + ARENAS_ARRAY_SIZE;
    if (arrayLimit < candidate) {
      return false;
    }
    Offset totalSpansFound = 0;
    Offset nextBaseForArena = 0;
    for (Offset checkArray = arrayStart; checkArray < arrayLimit;
         checkArray += sizeof(Offset)) {
      Offset baseForArena = nextBaseForArena;
      nextBaseForArena += ARENA_SIZE;
      Offset heapArenaCandidate = arenasReader.ReadOffset(checkArray, 1);
      if (heapArenaCandidate == 0) {
        continue;
      }
      if ((heapArenaCandidate & 0xfff) != 0) {
        return false;
      }
      Offset numSpansFound = 0;
      if (totalSpansFound == 0) {
        ClearValuesDerivedFromSingleSpan();
      }
      if (!IsPlausibleHeapArena(heapArenaCandidate, baseForArena,
                                numSpansFound)) {
        return false;
      }
      totalSpansFound += numSpansFound;
    }
    return (totalSpansFound > 0);
  }

  bool FindArenasField(
      const typename ModuleDirectory<Offset>::ModuleInfo& moduleInfo) {
    Reader moduleReader(_virtualAddressMap);
    Reader arenasReader(_virtualAddressMap);

    const auto& ranges = moduleInfo._ranges;
    if (ranges.empty()) {
      return false;
    }
    for (const auto& range : ranges) {
      int flags = range._value._flags;
      if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      Offset limit = range._limit;
      for (Offset check = range._base; check < limit; check += sizeof(Offset)) {
        Offset arenasFieldCandidate = moduleReader.ReadOffset(check, 0);
        _spansInHeapArena = 0x200000;
        if (IsPlausibleArenasFieldValue(arenasFieldCandidate, arenasReader)) {
          _arenasFieldValue = arenasFieldCandidate;
          if (FindSizes(range._base, range._limit, moduleReader)) {
            return true;
          }
          std::cerr << "GoLang is probably present but the sizes array "
                    << "wasn't found.\n";
          return false;
        }
        _spansInHeapArena = 0;
        if (IsPlausibleArenasFieldValue(arenasFieldCandidate, arenasReader)) {
          _arenasFieldValue = arenasFieldCandidate;
          if (FindSizes(range._base, range._limit, moduleReader)) {
            return true;
          }
          std::cerr << "GoLang is probably present, but the sizes array "
                    << "wasn't found.\n";
          return false;
        }
      }
    }
    _spansInHeapArena = NOT_A_FIELD_OFFSET;
    ClearValuesDerivedFromSingleSpan();
    return false;
  }

  void FindPageLimitsAndRegisterMappedPages() {
    std::unique_ptr<MappedPageRangeIterator<Offset> > iterator;
    /*
     * Set the _firstMappedPage and the _lastMappedPage to the highest
     * and lowest they could possibly be, respectively, because we
     * need to supply those limits when we create the PageMapIterator
     * and we haven't yet derived the actual limits yet.
     */

    _firstMappedPage = 0;
    _lastMappedPage =
        (((Offset)(1)) << (_arenasIndexBits + _spansIndexBits)) - 1;

    iterator.reset(MakeMappedPageRangeIterator());
    if (iterator->Finished()) {
      abort();
    }

    Offset firstPageForRange = iterator->FirstPageForRange();
    _firstMappedPage = firstPageForRange;

    while (true) {
      _lastMappedPage = firstPageForRange + iterator->NumPagesForRange() - 1;
      Offset address = iterator->FirstAddressForRange();
      Offset size = iterator->RangeSize();
      const char* rangeType =
          (iterator->Mspan() == 0) ? GOLANG_MAPPED_PAGES : GOLANG_SPAN;
      if (!_virtualMemoryPartition.ClaimRange(address, size, rangeType,
                                              false)) {
        std::cerr << "Warning: unexpected overlap for " << rangeType
                  << " at [0x" << std::hex << address << ", 0x"
                  << (address + size) << ").\n";
      }
      iterator->Advance();
      if (iterator->Finished()) {
        break;
      }
      firstPageForRange = iterator->FirstPageForRange();
    }
  }
};
}  // namespace GoLang
}  // namespace chap
