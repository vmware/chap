// Copyright (c) 2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ModuleDirectory.h"
#include "../UnfilledImages.h"
#include "../VirtualAddressMap.h"
#include "../VirtualMemoryPartition.h"
#include "PageMapIterator.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace chap {
namespace TCMalloc {
template <class Offset>
class InfrastructureFinder {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename std::set<Offset> OffsetSet;
  static constexpr Offset NOT_A_PAGE = ~((Offset)0);
  static constexpr Offset NOT_A_FIELD_OFFSET = ~((Offset)0);
  static constexpr Offset NOT_A_MAPPED_ADDRESS = ~((Offset)0);

  InfrastructureFinder(VirtualMemoryPartition<Offset>& virtualMemoryPartition,
                       const ModuleDirectory<Offset>& moduleDirectory,
                       UnfilledImages<Offset>& unfilledImages)
      : TC_MALLOC_MAPPED_PAGES("tc malloc mapped pages"),
        _virtualMemoryPartition(virtualMemoryPartition),
        _moduleDirectory(moduleDirectory),
        _unfilledImages(unfilledImages),
        _pageMap(0),
        _pageMapDepth(0),
        _firstMappedPage(NOT_A_PAGE),
        _lastMappedPage(NOT_A_PAGE),
        _sizes(0),
        _numSizes(0),
        _addressMap(virtualMemoryPartition.GetAddressMap()),
        _unfilledImagesFound(false) {}

  const char* TC_MALLOC_MAPPED_PAGES;

  void Resolve() {
    if (!FindPageMapAndSizeArray()) {
      return;
    }
    std::cerr << "Warning: TC malloc is used here but is not yet "
                 "fully supported by chap.\n";
    FindPageLimitsAndRegisterMappedPages();
  }

  PageMapIterator<Offset>* MakePageMapIterator() const {
    return new PageMapIterator<Offset>(
        _addressMap, _pageMap, _pageMapDepth, _firstMappedPage, _lastMappedPage,
        _simpleLeaf, _firstPageFieldInSpan, _numPagesFieldInSpan,
        _compactSizeClassFieldInSpan, _locationAndSampledBitInSpan,
        _locationMask, _sizeOfCompactSizeClass, _spansInLeaf, _pageMapIndexBits,
        _middleNodeIndexBits, _leafIndexBits, _pageOffsetBits, _sizes,
        _numSizes);
  }

  Offset GetPageMap() const { return _pageMap; }
  Offset GetPageMapDepth() const { return _pageMapDepth; }
  Offset GetFirstMappedPage() const { return _firstMappedPage; }
  Offset GetLastMappedPage() const { return _lastMappedPage; }
  bool SimpleLeaf() const { return _simpleLeaf; }
  Offset GetFirstPageFieldInSpan() const { return _firstPageFieldInSpan; }
  Offset GetNumPagesFieldInSpan() const { return _numPagesFieldInSpan; }
  Offset GetFreeAllocationListInSpan() const {
    return _freeAllocationListInSpan;
  }
  Offset GetBitMapOrCacheInSpan() const {
    return _bitMapOrCacheInSpan;
  }
  Offset GetCacheSizeInSpan() const { return _cacheSizeInSpan; }
  Offset GetFreeObjectIndexInSpan() const { return _freeObjectIndexInSpan; }
  Offset GetEmbedCountInSpan() const { return _embedCountInSpan; }
  Offset GetCompactSizeClassFieldInSpan() const {
    return _compactSizeClassFieldInSpan;
  }
  Offset GetUsedObjectCountInSpan() const {
    return _usedObjectCountInSpan;
  }
  Offset GetLocationAndSampledBitInSpan() const {
    return _locationAndSampledBitInSpan;
  }
  unsigned char GetLocationMask() const {
    return _locationMask;
  }
  Offset GetSizeOfCompactSizeClass() const { return _sizeOfCompactSizeClass; }
  Offset GetSpansInLeaf() const { return _spansInLeaf; }
  Offset GetPageMapIndexBits() const { return _pageMapIndexBits; }
  Offset GetMiddleNodeIndexBits() const { return _middleNodeIndexBits; }
  Offset GetLeafIndexBits() const { return _leafIndexBits; }
  Offset GetPageOffsetBits() const { return _pageOffsetBits; }
  Offset GetSizes() const { return _sizes; }
  Offset GetNumSizes() const { return _numSizes; }

 private:
  static constexpr Offset MAPPED_ADDRESS_BITS = 48;
  static constexpr Offset COMPOUND_LEAF_INDEX_BITS = 15;
  static constexpr Offset PAGES_PER_COMPOUND_LEAF = 1
                                                    << COMPOUND_LEAF_INDEX_BITS;
  static constexpr Offset SIMPLE_LEAF_INDEX_BITS = 18;
  static constexpr Offset PAGES_PER_SIMPLE_LEAF = 1 << SIMPLE_LEAF_INDEX_BITS;

   // TODO: These 3 should be derived because a later release moves the first of these
   // three past the other two.
  static constexpr Offset USED_OBJECT_COUNT_IN_GOOGLE_TCMALLOC_SPAN = 0x10;
  static constexpr Offset EMBED_COUNT_IN_GOOGLE_TCMALLOC_SPAN = 0x12;
  static constexpr Offset FREE_OBJECT_INDEX_IN_GOOGLE_TCMALLOC_SPAN = 0x14;

  static constexpr Offset CACHE_SIZE_IN_GOOGLE_TCMALLOC_SPAN = 0x16;
  static constexpr Offset LOCATION_AND_SAMPLED_BIT_IN_GOOGLE_TCMALLOC_SPAN =
      0x17;

   // TODO: These 3 should be derived because a later rewlates moves the first of these
   // three past the other two.
  static constexpr Offset BIT_MAP_OR_CACHE_IN_GOOGLE_TCMALLOC_SPAN = 0x18;
  static constexpr Offset FIRST_PAGE_FIELD_IN_GOOGLE_TCMALLOC_SPAN = 0x20;
  static constexpr Offset NUM_PAGES_FIELD_IN_GOOGLE_TCMALLOC_SPAN = 0x28;

  static constexpr unsigned char GOOGLE_TCMALLOC_LOCATION_MASK = 0x30;
  static constexpr Offset FIRST_PAGE_FIELD_IN_GPERFTOOLS_SPAN = 0;
  static constexpr Offset NUM_PAGES_FIELD_IN_GPERFTOOLS_SPAN = 8;
  static constexpr Offset FREE_ALLOCATION_LIST_IN_GPERFTOOLS_SPAN = 0x20;
  static constexpr Offset USED_OBJECT_COUNT_IN_GPERFTOOLS_SPAN = 0x28;
  static constexpr Offset COMPACT_SIZE_CLASS_FIELD_IN_GPERFTOOLS_SPAN = 0x2a;
  static constexpr Offset LOCATION_AND_SAMPLED_BIT_IN_GPERFTOOLS_SPAN = 0x2b;
  static constexpr unsigned char GPERFTOOLS_LOCATION_MASK = 3;
  static constexpr Offset PAGEMAP3_SIZE = sizeof(Offset) << 11;
  static constexpr Offset PAGEMAP3_LEAF_HOLDER_SIZE = sizeof(Offset) << 11;
  static constexpr Offset MINIMUM_PAGEMAP_SIZE = sizeof(Offset) << 15;
  static constexpr Offset PAGE_HEAP_LEAVES_FIELD_SIZE = sizeof(Offset) << 17;
  VirtualMemoryPartition<Offset>& _virtualMemoryPartition;
  const ModuleDirectory<Offset>& _moduleDirectory;
  UnfilledImages<Offset>& _unfilledImages;

  Offset _pageMap;
  Offset _pageMapDepth;
  Offset _firstMappedPage;
  Offset _lastMappedPage;

  bool _simpleLeaf;
  Offset _firstPageFieldInSpan;
  Offset _numPagesFieldInSpan;
  Offset _freeAllocationListInSpan;
  Offset _bitMapOrCacheInSpan;
  Offset _cacheSizeInSpan;
  Offset _freeObjectIndexInSpan;
  Offset _embedCountInSpan;
  Offset _compactSizeClassFieldInSpan;
  Offset _usedObjectCountInSpan;
  Offset _locationAndSampledBitInSpan;
  unsigned char _locationMask;
  Offset _sizeOfCompactSizeClass;
  Offset _spansInLeaf;

  Offset _pageMapIndexBits;
  Offset _middleNodeIndexBits;
  Offset _leafIndexBits;
  Offset _pageOffsetBits;

  Offset _sizes;
  Offset _numSizes;
  const VirtualAddressMap<Offset>& _addressMap;

  static constexpr Offset OFFSET_SIZE = sizeof(Offset);
  bool _unfilledImagesFound;

  void FindPageLimitsAndRegisterMappedPages() {
    std::unique_ptr<PageMapIterator<Offset> > pageMapIterator;
    /*
     * Set the _firstMappedPage and the _lastMappedPage to the highest
     * and lowest they could possibly be, respectively, because we
     * need to supply those limits when we create the PageMapIterator
     * and we haven't yet derived the actual limits yet.
     */

    _firstMappedPage = 0;
    _lastMappedPage =
        (((Offset)(1)) << (_pageMapIndexBits + _middleNodeIndexBits +
                           _leafIndexBits)) -
        1;

    pageMapIterator.reset(MakePageMapIterator());
    if (pageMapIterator->Finished()) {
      abort();
    }

    Offset firstPageForSpan = pageMapIterator->FirstPageForSpan();
    _firstMappedPage = firstPageForSpan;

    while(true) {
      _lastMappedPage =
          firstPageForSpan + pageMapIterator->NumPagesForSpan() - 1;
      ReserveMappedPageRange(pageMapIterator->FirstAddressForSpan(),
                             pageMapIterator->SpanSize());
      pageMapIterator->Advance();
      if (pageMapIterator->Finished()) {
        break;
      }
      firstPageForSpan = pageMapIterator->FirstPageForSpan();
    }
  }

  void ReserveMappedPageRange(Offset address, Offset size) {
    if (!_virtualMemoryPartition.ClaimRange(address, size,
                                            TC_MALLOC_MAPPED_PAGES, false)) {
      std::cerr
          << "Warning: unexpected overlap for tcmalloc mapped pages at [0x"
          << std::hex << address << ", 0x" << (address + size) << ").\n";
    }
   }


   template <typename CompactSizeClass>
   bool IsValidCompoundLeaf(Offset leafCandidate, Offset& numSpansFound,
                            Offset& firstPageNumber, Reader spanReader) {
     firstPageNumber = NOT_A_PAGE;
     if ((leafCandidate & (sizeof(Offset) - 1)) != 0) {
       return false;
     }
     numSpansFound = 0;
     Offset firstPageInLeaf = NOT_A_PAGE;
     auto it = _addressMap.find(leafCandidate);
     if (it == _addressMap.end()) {
       return false;
     }
     if ((it.Flags() & RangeAttributes::IS_WRITABLE) == 0) {
       return false;
     }
     const char* image = it.GetImage();
     if (image == nullptr) {
       return false;
     }

     if ((it.Limit() - leafCandidate) <
         ((sizeof(Offset) + sizeof(CompactSizeClass)) *
          PAGES_PER_COMPOUND_LEAF)) {
       return false;
     }

     const char* leafImage = image + (leafCandidate - it.Base());
     const CompactSizeClass* sizes = (const CompactSizeClass*)(leafImage);
     const Offset* spans = (const Offset*)(sizes + PAGES_PER_COMPOUND_LEAF);
     Offset prevSpan = 0;
     for (Offset index = 0; index < PAGES_PER_COMPOUND_LEAF; index++) {
       CompactSizeClass compactSizeClass = sizes[index];
       Offset span = spans[index];
       if (compactSizeClass == 0) {
         prevSpan = 0;
         continue;
       }
       if (span == 0) {
         return false;
       }
       if (span == prevSpan) {
         continue;
       }
       Offset firstPage =
           spanReader.ReadOffset(span + _firstPageFieldInSpan, ~index);
       if ((firstPage & (PAGES_PER_COMPOUND_LEAF - 1)) != index) {
         return false;
       }
       Offset newFirstPageInLeaf = firstPage & ~(PAGES_PER_COMPOUND_LEAF - 1);
       if (firstPageInLeaf != newFirstPageInLeaf) {
         firstPageInLeaf = newFirstPageInLeaf;
       }
       numSpansFound++;
       prevSpan = span;
     }
     if (numSpansFound > 0) {
       firstPageNumber = firstPageInLeaf;
     }
     return true;
  }

  void ResolvePageMap2Parameters(Offset base, Offset limit, bool simpleLeaf,
                                 Offset sizeOfCompactSizeClass) {
    _pageMap = base;
    _pageMapDepth = 2;
    _middleNodeIndexBits = 0;
    _simpleLeaf = simpleLeaf;
    _sizeOfCompactSizeClass = sizeOfCompactSizeClass;
    if (simpleLeaf) {
      _pageMapIndexBits = 17;
      _leafIndexBits = SIMPLE_LEAF_INDEX_BITS;
      _spansInLeaf = 0;
    } else {
      Offset maxPageMapSize = limit - base;
      if (maxPageMapSize >= 0x1000000) {
        _pageMapIndexBits = 21;
      } else if (maxPageMapSize >= 0x800000) {
        _pageMapIndexBits = 20;
      } else if (maxPageMapSize >= 0x200000) {
        _pageMapIndexBits = 18;
      } else {
        _pageMapIndexBits = 15;
      }
      _leafIndexBits = COMPOUND_LEAF_INDEX_BITS;
      _spansInLeaf = sizeOfCompactSizeClass << _leafIndexBits;
    }
    _pageOffsetBits = MAPPED_ADDRESS_BITS - _pageMapIndexBits - _leafIndexBits;
  }

  bool FindSizeClasses(Offset start, Offset limit, Reader& reader) {
    size_t sequenceLength = 0;
    uint32_t lastSize = 0;
    Offset check;
    for (check = start; limit; check += 4) {
      uint32_t size = reader.ReadU32(check, 0xbad);
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
      _sizes = check - sequenceLength * 4;
      _numSizes = sequenceLength;
      return true;
    }
    return false;
  }

  template <typename CompactSizeClass>
  bool FindCompoundLeafPageMap2(Reader& reader, Offset base, Offset limit) {
    Reader spanReader(_addressMap);
    Offset firstLeafPointerCandidate = 0;
    Offset lastLeafPointerCandidate = 0;
    Offset totalSpansFound = 0;
    for (Offset leafPointerCandidate = base; leafPointerCandidate < limit;
         leafPointerCandidate += sizeof(Offset)) {
      Offset leafCandidate = reader.ReadOffset(leafPointerCandidate, 0xbad);
      if (leafCandidate == 0) {
        continue;
      }

      Offset numSpansFound = 0;
      Offset firstPageNumber = NOT_A_PAGE;
      if (!IsValidCompoundLeaf<CompactSizeClass>(leafCandidate, numSpansFound,
                                                 firstPageNumber, spanReader)) {
        if (lastLeafPointerCandidate != 0) {
          if ((leafPointerCandidate - base) >= MINIMUM_PAGEMAP_SIZE) {
            if (totalSpansFound > 0) {
              ResolvePageMap2Parameters(base, limit, false,
                                        sizeof(CompactSizeClass));
              return true;
            }
          }
          base = leafPointerCandidate + sizeof(Offset);
          if (limit - base < MINIMUM_PAGEMAP_SIZE) {
            return false;
          }
        }
        firstLeafPointerCandidate = 0;
        lastLeafPointerCandidate = 0;
        totalSpansFound = 0;
        continue;
      }
      if (numSpansFound == 0) {
        continue;
      }
      totalSpansFound += numSpansFound;
      if (firstPageNumber != NOT_A_PAGE) {
        Offset indexInPageMap = firstPageNumber >> COMPOUND_LEAF_INDEX_BITS;
        Offset indexOfLeafPointerCandidate =
            (leafPointerCandidate - base) / sizeof(Offset);
        if (indexInPageMap != indexOfLeafPointerCandidate) {
          if (indexInPageMap > indexOfLeafPointerCandidate) {
            return false;
          }
          if (firstLeafPointerCandidate == 0) {
            base +=
                (indexOfLeafPointerCandidate - indexInPageMap) * sizeof(Offset);
          } else {
            return false;
          }
        }
      }

      if (firstLeafPointerCandidate == 0) {
        firstLeafPointerCandidate = leafPointerCandidate;
      }
      lastLeafPointerCandidate = leafPointerCandidate;
    }
    if ((lastLeafPointerCandidate != 0) &&
        ((limit - base) >= MINIMUM_PAGEMAP_SIZE)) {
      if (totalSpansFound > 0) {
        ResolvePageMap2Parameters(base, limit, false, sizeof(CompactSizeClass));
        return true;
      }
    }
    return false;
  }

  bool FindPageMap(Reader& moduleReader, Offset runBase, Offset runLimit) {
    _firstPageFieldInSpan = FIRST_PAGE_FIELD_IN_GOOGLE_TCMALLOC_SPAN;
    _numPagesFieldInSpan = NUM_PAGES_FIELD_IN_GOOGLE_TCMALLOC_SPAN;
    _freeAllocationListInSpan = NOT_A_FIELD_OFFSET;
    _bitMapOrCacheInSpan = BIT_MAP_OR_CACHE_IN_GOOGLE_TCMALLOC_SPAN;
    _cacheSizeInSpan = CACHE_SIZE_IN_GOOGLE_TCMALLOC_SPAN;
    _freeObjectIndexInSpan = FREE_OBJECT_INDEX_IN_GOOGLE_TCMALLOC_SPAN;
    _embedCountInSpan = EMBED_COUNT_IN_GOOGLE_TCMALLOC_SPAN;
    _compactSizeClassFieldInSpan = NOT_A_FIELD_OFFSET;
    _usedObjectCountInSpan = USED_OBJECT_COUNT_IN_GOOGLE_TCMALLOC_SPAN;
    _locationAndSampledBitInSpan =
        LOCATION_AND_SAMPLED_BIT_IN_GOOGLE_TCMALLOC_SPAN;
    _locationMask = GOOGLE_TCMALLOC_LOCATION_MASK;
    if (FindCompoundLeafPageMap2<unsigned char>(moduleReader, runBase,
                                                runLimit)) {
      return true;
    }
    if (FindCompoundLeafPageMap2<unsigned short>(moduleReader, runBase,
                                                 runLimit)) {
      return true;
    }
    _firstPageFieldInSpan = FIRST_PAGE_FIELD_IN_GPERFTOOLS_SPAN;
    _numPagesFieldInSpan = NUM_PAGES_FIELD_IN_GPERFTOOLS_SPAN;
    _freeAllocationListInSpan = FREE_ALLOCATION_LIST_IN_GPERFTOOLS_SPAN;
    _bitMapOrCacheInSpan = NOT_A_FIELD_OFFSET;
    _cacheSizeInSpan = NOT_A_FIELD_OFFSET;
    _freeObjectIndexInSpan = NOT_A_FIELD_OFFSET;
    _embedCountInSpan = NOT_A_FIELD_OFFSET;
    _compactSizeClassFieldInSpan = COMPACT_SIZE_CLASS_FIELD_IN_GPERFTOOLS_SPAN;
    _usedObjectCountInSpan = USED_OBJECT_COUNT_IN_GPERFTOOLS_SPAN;
    _locationAndSampledBitInSpan =
        LOCATION_AND_SAMPLED_BIT_IN_GPERFTOOLS_SPAN;
    _locationMask = GPERFTOOLS_LOCATION_MASK;
    if (FindSimpleLeafPageMap2(moduleReader, runBase, runLimit)) {
      return true;
    }
    return false;
  }

  bool FindPageMapAndSizeArray() {
    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      const auto& moduleInfo = nameAndModuleInfo.second;
      Reader moduleReader(_addressMap);

      const auto& ranges = moduleInfo._ranges;
      if (ranges.empty()) {
        return false;
      }
      for (const auto& range : ranges) {
        int flags = range._value._flags;
        if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
          continue;
        }
        Offset base = range._base;
        Offset limit = range._limit;
        if ((limit - base) < MINIMUM_PAGEMAP_SIZE) {
          continue;
        }
        Offset runStart = 0;

        for (Offset moduleAddr = base; moduleAddr < limit;
             moduleAddr += sizeof(Offset)) {
          Offset pointerOrNull = moduleReader.ReadOffset(moduleAddr, 0xbad);
          if ((pointerOrNull & (sizeof(Offset) - 1)) == 0) {
            if (runStart == 0) {
              runStart = moduleAddr;
            }
            continue;
          }
          if (runStart == 0) {
            continue;
          }
          Offset runSize = moduleAddr - runStart;
          if (runSize >= MINIMUM_PAGEMAP_SIZE) {
            if (FindPageMap(moduleReader, runStart, moduleAddr)) {
              if (FindSizeClasses(range._base, range._limit, moduleReader)) {
                return true;
              }
              std::cerr << "Warning: TC malloc might be present but "
                           "the size classes couldn't be found.\n";
              return false;
            }
          }
          runStart = 0;
        }
      }
    }
    return false;
  }

  bool IsValidSimpleLeaf(Offset leafCandidate, Offset& numSpansFound,
                         Offset& firstPageNumber, Reader spanReader) {
    firstPageNumber = NOT_A_PAGE;
    if ((leafCandidate & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    numSpansFound = 0;
    Offset firstPageInLeaf = NOT_A_PAGE;
    auto it = _addressMap.find(leafCandidate);
    if (it == _addressMap.end()) {
      return false;
    }
    if ((it.Flags() & RangeAttributes::IS_WRITABLE) == 0) {
      return false;
    }
    const char* image = it.GetImage();
    if (image == nullptr) {
      return false;
    }

    if ((it.Limit() - leafCandidate) <
        (sizeof(Offset) * PAGES_PER_SIMPLE_LEAF)) {
      return false;
    }

    const char* leafImage = image + (leafCandidate - it.Base());
    const Offset* spans = (const Offset*)(leafImage);
    Offset prevSpan = 0;
    for (Offset index = 0; index < PAGES_PER_SIMPLE_LEAF; index++) {
      Offset span = spans[index];
      if (span == 0) {
        prevSpan = span;
        continue;
      }
      if (span == prevSpan) {
        continue;
      }
      Offset firstPage =
          spanReader.ReadOffset(span + _firstPageFieldInSpan, ~index);
      Offset numPages =
          spanReader.ReadOffset(span + _numPagesFieldInSpan, ~index);
      if ((firstPage & (PAGES_PER_SIMPLE_LEAF - 1)) != index) {
        if (numSpansFound > 500) {
          // TODO: fix this.  The issue is that with a current leaf, we would
          // skip such a check if the compactSizeClass were 0, but we can't yet
          // do that because we don't know where to find the size classes yet.
          continue;
        }
        return false;
      }
      Offset newFirstPageInLeaf = firstPage & ~(PAGES_PER_SIMPLE_LEAF - 1);
      if (firstPageInLeaf != newFirstPageInLeaf) {
        firstPageInLeaf = newFirstPageInLeaf;
      }
      numSpansFound++;
      prevSpan = span;
      if (numPages > 1) {
        Offset lastIndex = index + numPages - 1;
        if (lastIndex < index) {
          return false;
        }
        index = lastIndex;
      }
    }
    if (numSpansFound > 0) {
      firstPageNumber = firstPageInLeaf;
    }
    return true;
  }

  bool FindSimpleLeafPageMap2(Reader& reader, Offset base, Offset limit) {
    Reader spanReader(_addressMap);
    Offset firstLeafPointerCandidate = 0;
    Offset lastLeafPointerCandidate = 0;
    Offset totalSpansFound = 0;
    for (Offset leafPointerCandidate = base; leafPointerCandidate < limit;
         leafPointerCandidate += sizeof(Offset)) {
      Offset leafCandidate = reader.ReadOffset(leafPointerCandidate, 0xbad);
      if (leafCandidate == 0) {
        continue;
      }

      Offset numSpansFound = 0;
      Offset firstPageNumber = NOT_A_PAGE;
      if (!IsValidSimpleLeaf(leafCandidate, numSpansFound, firstPageNumber,
                             spanReader)) {
        if (lastLeafPointerCandidate != 0) {
          if ((leafPointerCandidate - base) >= PAGE_HEAP_LEAVES_FIELD_SIZE) {
            if (totalSpansFound > 0) {
              ResolvePageMap2Parameters(base, limit, true, 1);
              return true;
            }
          }
          base = leafPointerCandidate + sizeof(Offset);
          if (limit - base < PAGE_HEAP_LEAVES_FIELD_SIZE) {
            return false;
          }
        }
        firstLeafPointerCandidate = 0;
        lastLeafPointerCandidate = 0;
        totalSpansFound = 0;
        continue;
      }
      totalSpansFound += numSpansFound;
      if (firstPageNumber != NOT_A_PAGE) {
        Offset indexInPageMap = firstPageNumber >> SIMPLE_LEAF_INDEX_BITS;
        Offset indexOfLeafPointerCandidate =
            (leafPointerCandidate - base) / sizeof(Offset);
        if (indexInPageMap != indexOfLeafPointerCandidate) {
          if (indexInPageMap > indexOfLeafPointerCandidate) {
            return false;
          }
          if (firstLeafPointerCandidate == 0) {
            base +=
                (indexOfLeafPointerCandidate - indexInPageMap) * sizeof(Offset);
          } else {
            return false;
          }
        }
      }

      if (firstLeafPointerCandidate == 0) {
        firstLeafPointerCandidate = leafPointerCandidate;
      }
      lastLeafPointerCandidate = leafPointerCandidate;
    }
    if (totalSpansFound > 0) {
      ResolvePageMap2Parameters(base, limit, true, 1);
      return true;
    }
    return false;
  }

  template <typename CompactSizeClass>
  bool IsValidPageMap3LeafHolder(Offset leafHolderCandidate,
                                 Offset numSpansFound, Reader& leafHolderReader,
                                 Reader& spanReader) {
    Offset limit = leafHolderCandidate + PAGEMAP3_LEAF_HOLDER_SIZE;
    numSpansFound = 0;
    for (Offset leafPointerCandidate = leafHolderCandidate;
         leafPointerCandidate != limit;
         leafPointerCandidate += sizeof(Offset)) {
      Offset leafCandidate =
          leafHolderReader.ReadOffset(leafPointerCandidate, 0xbad);
      if (leafCandidate == 0) {
        continue;
      }
      if ((leafCandidate & (sizeof(Offset) - 1)) != 0) {
        return false;
      }
      Offset firstPageNumber = NOT_A_PAGE;
      Offset spansInLeaf = 0;
      if (!IsValidCompoundLeaf<CompactSizeClass>(leafCandidate, spansInLeaf,
                                                 firstPageNumber, spanReader)) {
        return false;
      }
      numSpansFound += spansInLeaf;
      // TODO: plumb firstPageNumber out a level (for use in calculating start
      // of PageMap3)
      // and check for consistency within the leaf holder.
    }
    return true;
  }

  template <typename CompactSizeClass>
  bool FindPageMap3InRange(Reader& reader, Offset base, Offset limit) {
    Reader leafHolderReader(_addressMap);
    Reader spanReader(_addressMap);
    Offset firstLeafHolderPointerCandidate = 0;
    Offset lastLeafHolderPointerCandidate = 0;
    Offset totalSpansFound = 0;
    for (Offset leafHolderPointerCandidate = base;
         leafHolderPointerCandidate < limit;
         leafHolderPointerCandidate += sizeof(Offset)) {
      Offset leafHolderCandidate =
          reader.ReadOffset(leafHolderPointerCandidate, 0xbad);
      if (leafHolderCandidate == 0) {
        continue;
      }
      Offset numSpansFound;
      if (!IsValidPageMap3LeafHolder<CompactSizeClass>(
              leafHolderCandidate, numSpansFound, leafHolderReader,
              spanReader)) {
        if (lastLeafHolderPointerCandidate != 0) {
          if ((leafHolderPointerCandidate - base) >= PAGEMAP3_SIZE) {
            if (totalSpansFound > 0) {
              return true;
            }
          }
          base = leafHolderPointerCandidate + sizeof(Offset);
          if (limit - base < PAGEMAP3_SIZE) {
            return false;
          }
        }
        firstLeafHolderPointerCandidate = 0;
        lastLeafHolderPointerCandidate = 0;
        totalSpansFound = 0;
        continue;
      }
      totalSpansFound += numSpansFound;
      if (firstLeafHolderPointerCandidate == 0) {
        firstLeafHolderPointerCandidate = leafHolderPointerCandidate;
      }
      lastLeafHolderPointerCandidate = leafHolderPointerCandidate;
    }
    if ((lastLeafHolderPointerCandidate != 0) &&
        ((limit - base) >= PAGEMAP3_SIZE)) {
      if (totalSpansFound > 0) {
        return true;
      }
    }
    return false;
  }

  bool FindPageMap3() {
    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      const std::string& modulePath = nameAndModuleInfo.first;
      const auto& moduleInfo = nameAndModuleInfo.second;
      Reader moduleReader(_addressMap);

      const auto& ranges = moduleInfo._ranges;
      if (ranges.empty()) {
        return false;
      }
      for (const auto& range : ranges) {
        int flags = range._value._flags;
        if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
          continue;
        }
        Offset base = range._base;
        Offset limit = range._limit;
        if ((limit - base) < PAGEMAP3_SIZE) {
          continue;
        }
        Offset runStart = 0;
        size_t numPointers;

        for (Offset moduleAddr = base; moduleAddr < limit;
             moduleAddr += sizeof(Offset)) {
          Offset pointerOrNull = moduleReader.ReadOffset(moduleAddr, 0xbad);
          if ((pointerOrNull & (sizeof(Offset) - 1)) == 0) {
            if (pointerOrNull != 0) {
              numPointers++;
            }
            if (runStart == 0) {
              runStart = moduleAddr;
            }
            continue;
          }
          if (runStart == 0) {
            continue;
          }
          Offset runSize = moduleAddr - runStart;
          if (runSize >= PAGEMAP3_SIZE) {
            std::cerr << "Possible PageMap3 in [0x" << std::hex << runStart
                      << ", 0x" << moduleAddr << ") for module " << modulePath
                      << " with " << std::dec << numPointers << "pointers.\n";
            if (FindPageMap3InRange<unsigned char>(moduleReader, runStart,
                                                   moduleAddr)) {
              _sizeOfCompactSizeClass = 1;
              return true;
            }
            if (FindPageMap3InRange<unsigned short>(moduleReader, runStart,
                                                    moduleAddr)) {
              _sizeOfCompactSizeClass = 2;
              return true;
            }
          }
          runStart = 0;
          numPointers = 0;
        }
      }
    }
    return false;
  }
};

}  // namespace TCMalloc
}  // namespace chap
