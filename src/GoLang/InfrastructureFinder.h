// Copyright (c) 2020,2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <algorithm>
#include "../ModuleDirectory.h"
#include "../StackRegistry.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"

namespace chap {
namespace GoLang {
template <typename Offset>
class InfrastructureFinder {
 public:
  static constexpr Offset STACK_BASE_IN_GOROUTINE = 0;
  static constexpr Offset STACK_LIMIT_IN_GOROUTINE =
      STACK_BASE_IN_GOROUTINE + sizeof(Offset);
  static constexpr Offset STACK_POINTER_IN_GOROUTINE = 7 * sizeof(Offset);
  static constexpr Offset NEXT_IN_MSPAN = 0;
  static constexpr Offset PREV_IN_MSPAN = NEXT_IN_MSPAN + sizeof(Offset);
  static constexpr Offset LIST_IN_MSPAN = PREV_IN_MSPAN + sizeof(Offset);
  static constexpr Offset START_ADDR_IN_MSPAN = LIST_IN_MSPAN + sizeof(Offset);
  static constexpr Offset NPAGES_IN_MSPAN =
      START_ADDR_IN_MSPAN + sizeof(Offset);
  static constexpr Offset MANUAL_FREE_LIST_IN_MSPAN =
      NPAGES_IN_MSPAN + sizeof(Offset);
  static constexpr Offset FREE_INDEX_IN_MSPAN =
      MANUAL_FREE_LIST_IN_MSPAN + sizeof(Offset);
  static constexpr Offset NELEMS_IN_MSPAN =
      FREE_INDEX_IN_MSPAN + sizeof(Offset);
  static constexpr Offset ALLOC_CACHE_IN_MSPAN =
      NELEMS_IN_MSPAN + sizeof(Offset);
  static constexpr Offset ALLOC_BITS_IN_MSPAN = ALLOC_CACHE_IN_MSPAN + 8;
  static constexpr Offset GC_MARK_BITS_IN_MSPAN =
      ALLOC_BITS_IN_MSPAN + sizeof(Offset);
  static constexpr Offset SWEEPGEN_IN_MSPAN =
      GC_MARK_BITS_IN_MSPAN + sizeof(Offset);
  static constexpr Offset DIV_MUL_IN_MSPAN = SWEEPGEN_IN_MSPAN + 4;
  static constexpr Offset BASE_MASK_IN_MSPAN = DIV_MUL_IN_MSPAN + 2;
  static constexpr Offset ALLOC_COUNT_IN_MSPAN = BASE_MASK_IN_MSPAN + 2;
  static constexpr Offset SPAN_CLASS_IN_MSPAN = ALLOC_COUNT_IN_MSPAN + 2;
  static constexpr Offset STATE_IN_CURRENT_MSPAN = SPAN_CLASS_IN_MSPAN + 1;
  static constexpr Offset STATE_IN_OLD_MSPAN = SPAN_CLASS_IN_MSPAN + 2;
  static constexpr Offset ELEM_SIZE_IN_MSPAN = SPAN_CLASS_IN_MSPAN + 6;
  static constexpr Offset LIMIT_IN_CURRENT_MSPAN =
      ELEM_SIZE_IN_MSPAN + sizeof(Offset);
  static constexpr Offset LIMIT_IN_OLD_MSPAN =
      ELEM_SIZE_IN_MSPAN + 3 * sizeof(Offset);
  static constexpr Offset PAGE_SHIFT = 13;
  static constexpr Offset PAGE_SIZE = 1 << PAGE_SHIFT;
  static constexpr Offset NUM_SPANS_UPPER_BOUND =
      ((~((Offset)0)) >> PAGE_SHIFT) + 1;
  static constexpr Offset NUM_PAGES_UPPER_BOUND = NUM_SPANS_UPPER_BOUND;
  static constexpr uint8_t SPAN_STATE_DEAD = 0;
  static constexpr uint8_t SPAN_STATE_IN_USE = 1;
  static constexpr uint8_t SPAN_STATE_MANUAL = 2;
  static constexpr uint8_t SPAN_STATE_FREE = 3;
  static constexpr uint8_t MAX_SPAN_STATE = SPAN_STATE_FREE;

  InfrastructureFinder(const ModuleDirectory<Offset>& moduleDirectory,
                       VirtualMemoryPartition<Offset>& partition,
                       StackRegistry<Offset>& stackRegistry)
      : GO_SPAN("go span"),
        GOROUTINE_STACK("goroutine stack"),
        _moduleDirectory(moduleDirectory),
        _isResolved(false),
        _virtualMemoryPartition(partition),
        _stackRegistry(stackRegistry),
        _virtualAddressMap(partition.GetAddressMap()),
        _goRoutines(0),
        _numGoRoutines(0),
        _mspans(0),
        _numSpans(0),
        _mheap(0),
        _stateInMspan(STATE_IN_CURRENT_MSPAN),
        _limitInMspan(LIMIT_IN_CURRENT_MSPAN) {}

  const char* GO_SPAN;
  const char* GOROUTINE_STACK;

  void Resolve() {
    if (_isResolved) {
      abort();
    }
    if (!_moduleDirectory.IsResolved()) {
      abort();
    }

    for (typename ModuleDirectory<Offset>::const_iterator it =
             _moduleDirectory.begin();
         it != _moduleDirectory.end(); ++it) {
      if (it->first.find(".so") != std::string::npos) {
        // For now, assume the go runtime code is not in a shared library.
        continue;
      }
      FindGoRoutines(it->second);
      if (_goRoutines == 0) {
        continue;
      }

      std::cerr << "Warning: This is a core for a GoLang process.\n"
                   "... GoLang allocations are not found yet, but libc malloc "
                   "allocations are.\n";

      /*
       * At this point, we have the header to the goroutines.
       */

      FindSpans(it->second);
      if (_mspans == 0) {
        _stateInMspan = STATE_IN_OLD_MSPAN;
        _limitInMspan = LIMIT_IN_OLD_MSPAN;
        FindSpans(it->second);
        if (_mspans == 0) {
          std::cerr << "Warning: Failed to find mspan pointers for apparent "
                       "GoLang process.\n";
          std::cerr << "This will prevent finding most allocations.\n";
        }
      }
    }
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  bool _isResolved;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  StackRegistry<Offset>& _stackRegistry;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  Offset _goRoutines;
  Offset _numGoRoutines;
  Offset _mspans;
  Offset _numSpans;
  Offset _mheap;
  Offset _stateInMspan;
  Offset _limitInMspan;
  std::vector<Offset> _mspansIndicesByStart;

  bool HasApparentGoRoutinePointer(Reader& reader, Offset pointerAddress) {
    Offset goRoutine = reader.ReadOffset(pointerAddress, 0xbad);
    if ((goRoutine & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
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
      Offset stackPointer =
          reader.ReadOffset(goRoutine + STACK_POINTER_IN_GOROUTINE, 0);
      if (stackPointer < stackBase || stackPointer >= stackLimit) {
        return false;
      }
    }
    return true;
  }

  void FindGoRoutines(
      const typename ModuleDirectory<Offset>::RangeToFlags& rangeToFlags) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (typename ModuleDirectory<Offset>::RangeToFlags::const_iterator
             itRange = rangeToFlags.begin();
         itRange != rangeToFlags.end(); ++itRange) {
      int flags = itRange->_value;
      if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      Offset base = itRange->_base;
      /*
       * At present the module finding logic can get a lower value for the
       * limit than the true limit.  It is conservative about selecting the
       * limit to avoid tagging too large a range in the partition.  However
       * this conservative estimate is problematic if the array header we
       * are seeking lies between the calculated limit and the real
       * limit.  This code works around this to extend the limit to the
       * last consecutive byte that has the same permission as the last
       * byte in the range.
       */
      Offset limit = _virtualAddressMap.find(itRange->_limit - 1).Limit();

      for (Offset moduleAddr = base; moduleAddr < limit;
           moduleAddr += sizeof(Offset)) {
        Offset arrayOfPointers = moduleReader.ReadOffset(moduleAddr, 0xbad);
        if ((arrayOfPointers & 7) != 0) {
          continue;
        }
        Offset size = moduleReader.ReadOffset(moduleAddr + sizeof(Offset), 0);
        Offset capacity =
            moduleReader.ReadOffset(moduleAddr + 2 * sizeof(Offset), 0);
        if (size < 4 || size > capacity) {
          continue;
        }

        if (HasApparentGoRoutinePointer(reader, arrayOfPointers) &&
            HasApparentGoRoutinePointer(
                reader, arrayOfPointers + (size - 1) * sizeof(Offset))) {
          _goRoutines = arrayOfPointers;
          _numGoRoutines = size;
          for (Offset i = 0; i < _numGoRoutines; i++) {
            Offset goRoutine =
                reader.ReadOffset(arrayOfPointers + i * sizeof(Offset), 0xbad);
            if ((goRoutine & (sizeof(Offset) - 1)) != 0) {
              continue;
            }
            Offset stackBase =
                reader.ReadOffset(goRoutine + STACK_BASE_IN_GOROUTINE, 0xbad);
            if ((stackBase & 0x3f) != 0) {
              continue;
            }
            Offset stackLimit =
                reader.ReadOffset(goRoutine + STACK_LIMIT_IN_GOROUTINE, 0xbad);
            if ((stackBase & 0x3f) != 0) {
              continue;
            }
            Offset stackPointer = reader.ReadOffset(
                goRoutine + STACK_POINTER_IN_GOROUTINE, 0xbad);
            if ((stackPointer & (sizeof(Offset) - 1)) != 0) {
              continue;
            }
            if (!_stackRegistry.RegisterStack(stackBase, stackLimit,
                                              GOROUTINE_STACK)) {
              std::cerr << "Failed to register stack at [0x" << std::hex
                        << stackBase << ", 0x" << stackLimit
                        << ") due to overlap with other stack.\n";
            }
            /*
             * Note: This may not be the most current stack pointer for a
             * thread that was running at the time of the core, but if not
             * it will be fixed at the time the thread is associated with
             * the goroutine.
             */
            if (stackPointer >= stackBase && stackPointer < stackLimit &&
                !_stackRegistry.AddStackTop(stackPointer)) {
              std::cerr << "Warning: Failed to set saved stack pointer for "
                           "goroutine at 0x"
                        << std::hex << goRoutine << "\n";
            }
          }
          return;
        }
      }
    }
  }

  bool HasApparentMspanPointer(Reader& reader, Offset pointerAddress) {
    Offset mspan = reader.ReadOffset(pointerAddress, 0xbad);
    if ((mspan & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    Offset startAddr = reader.ReadOffset(mspan + START_ADDR_IN_MSPAN, 0xbad);
    if (startAddr == 0 || (startAddr & (PAGE_SIZE - 1)) != 0) {
      return false;
    }
    Offset npages = reader.ReadOffset(mspan + NPAGES_IN_MSPAN, ~0);
    if (npages == 0 || npages >= NUM_PAGES_UPPER_BOUND) {
      return false;
    }
    Offset spanSize = npages << PAGE_SHIFT;
    Offset spanLimit = startAddr + spanSize;
    Offset nelems = reader.ReadOffset(mspan + NELEMS_IN_MSPAN, ~0);
    if (nelems > spanSize) {
      return 0;
    }
    Offset elementSize = reader.ReadOffset(mspan + ELEM_SIZE_IN_MSPAN, ~0);
    if (elementSize == ~((Offset)0)) {
      return false;
    }
    uint8_t state = reader.ReadU8(mspan + _stateInMspan, 92);
    if (state > MAX_SPAN_STATE) {
      return false;
    }
    Offset limit = reader.ReadOffset(mspan + _limitInMspan, ~0);
    if (state == SPAN_STATE_DEAD || state == SPAN_STATE_FREE) {
      if (reader.ReadU16(mspan + ALLOC_COUNT_IN_MSPAN, 1) != 0) {
        return false;
      }
    } else {
      if (nelems == 0) {
        if (limit != spanLimit) {
          return false;
        }
      } else {
        Offset sizeClass = reader.ReadU8(mspan + SPAN_CLASS_IN_MSPAN, ~0) >> 1;
        if (sizeClass == 0) {
          if (nelems != 1 || elementSize != spanSize) {
            return false;
          }
        } else {
          if (elementSize == 0 || elementSize > (spanSize / nelems)) {
            return false;
          }
          Offset pastElements = startAddr + nelems * elementSize;
          if (pastElements > spanLimit) {
            return false;
          }
          if (limit != pastElements) {
            return false;
          }
        }
      }
    }

    Offset next = reader.ReadOffset(mspan + NEXT_IN_MSPAN, 0xbad);
    if ((next & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    Offset prev = reader.ReadOffset(mspan + PREV_IN_MSPAN, 0xbad);
    if ((prev & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    Offset list = reader.ReadOffset(mspan + LIST_IN_MSPAN, 0xbad);
    if (list == 0) {
      if (prev != 0) {
        return false;
      }
    } else {
      if ((list & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
    }

    if (next != 0) {
      if (list != reader.ReadOffset(next + LIST_IN_MSPAN, 0xbad)) {
        return false;
      }
      Offset prevOfNext = reader.ReadOffset(next + PREV_IN_MSPAN, 0xbad);
      if (prevOfNext != 0 && prevOfNext != mspan) {
        return false;
      }
    }
    if (prev != 0 &&
        (mspan != reader.ReadOffset(prev, 0xbad) ||
         list != reader.ReadOffset(prev + 2 * sizeof(Offset), 0xbad))) {
      return false;
    }
    return true;
  }

  void FindSpans(
      const typename ModuleDirectory<Offset>::RangeToFlags& rangeToFlags) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (typename ModuleDirectory<Offset>::RangeToFlags::const_iterator
             itRange = rangeToFlags.begin();
         itRange != rangeToFlags.end(); ++itRange) {
      int flags = itRange->_value;
      if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
        continue;
      }
      Offset base = itRange->_base;
      /*
       * At present the module finding logic can get a lower value for the
       * limit than the true limit.  It is conservative about selecting the
       * limit to avoid tagging too large a range in the partition.  However
       * this conservative estimate is problematic if the array header we
       * are seeking lies between the calculated limit and the real
       * limit.  This code works around this to extend the limit to the
       * last consecutive byte that has the same permission as the last
       * byte in the range.
       */
      Offset limit = _virtualAddressMap.find(itRange->_limit - 1).Limit();

      for (Offset moduleAddr = base; moduleAddr < limit;
           moduleAddr += sizeof(Offset)) {
        Offset arrayOfPointers = moduleReader.ReadOffset(moduleAddr, 0xbad);
        if ((arrayOfPointers & 7) != 0) {
          continue;
        }
        Offset size = moduleReader.ReadOffset(moduleAddr + sizeof(Offset), 0);
        Offset capacity =
            moduleReader.ReadOffset(moduleAddr + 2 * sizeof(Offset), 0);
        if (size < 4 || size > capacity || size > NUM_SPANS_UPPER_BOUND) {
          continue;
        }

        if (HasApparentMspanPointer(reader, arrayOfPointers)) {
          if (!HasApparentMspanPointer(
                  reader, arrayOfPointers + (size - 1) * sizeof(Offset))) {
            continue;
          }
          Offset numMspans = 2;

          for (Offset i = size - 2; i > 0; i--) {
            if (HasApparentMspanPointer(reader,
                                        arrayOfPointers + i * sizeof(Offset))) {
              numMspans++;
            }
          }
          if (numMspans > size - 4) {
            _mspans = arrayOfPointers;
            _numSpans = size;
            _mspansIndicesByStart.reserve(size);
            _mspansIndicesByStart.resize(size, 0);
            Reader spanReader(_virtualAddressMap);
            for (Offset spanIndex = 0; spanIndex < _numSpans; ++spanIndex) {
              _mspansIndicesByStart[spanIndex] = spanIndex;
              Offset mspan =
                  reader.ReadOffset(_mspans + spanIndex * sizeof(Offset), 0);
              Offset spanStart =
                  spanReader.ReadOffset(mspan + START_ADDR_IN_MSPAN, 0);
              Offset numSpanPages =
                  spanReader.ReadOffset(mspan + NPAGES_IN_MSPAN, 0);
              Offset spanSize = numSpanPages << PAGE_SHIFT;
              if (!_virtualMemoryPartition.ClaimRange(spanStart, spanSize,
                                                      GO_SPAN, false)) {
                // TODO: Fix this.
                // std::cerr << "Warning: Failed to claim span range [" <<
                // std::hex
                //          << spanStart << ", " << (spanStart + spanSize)
                //          << ") due to overlap.\n";
              }
            }
            std::sort(
                _mspansIndicesByStart.begin(), _mspansIndicesByStart.end(),
                [&](size_t i0, size_t i1) {
                  Offset mspan0 =
                      reader.ReadOffset(this->_mspans + i0 * sizeof(Offset), 0);
                  Offset start0 =
                      spanReader.ReadOffset(mspan0 + START_ADDR_IN_MSPAN, 0);
                  Offset mspan1 =
                      reader.ReadOffset(this->_mspans + i1 * sizeof(Offset), 0);
                  Offset start1 =
                      spanReader.ReadOffset(mspan1 + START_ADDR_IN_MSPAN, 0);
                  return start0 < start1;
                });
            return;
          }
        }
      }
    }
  }
};
}  // namespace GoLang
}  // namespace chap
