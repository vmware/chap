// Copyright (c) 2019-2020,2023 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Describer.h"
#include "../ProcessImage.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace LibcMalloc {
template <typename Offset>
class HeapDescriber : public Describer<Offset> {
 public:
  typedef typename InfrastructureFinder<Offset>::Heap Heap;
  typedef typename InfrastructureFinder<Offset>::HeapMap HeapMap;
  typedef typename InfrastructureFinder<Offset>::HeapMapConstIterator
      HeapMapConstIterator;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  HeapDescriber(const InfrastructureFinder<Offset> &infrastructureFinder,
                const VirtualAddressMap<Offset> &addressMap)
      : _addressMap(addressMap),
        _maxHeapSize(infrastructureFinder.GetMaxHeapSize()),
        _heapHeaderSize(infrastructureFinder.GetHeapHeaderSize()),
        _heaps(infrastructureFinder.GetHeaps()),
        _arenaStructSize(infrastructureFinder.GetArenaStructSize()) {}
  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context &context, Offset address, bool explain,
                bool showAddresses) const {
    Offset heapStart = address & ~(_maxHeapSize - 1);
    HeapMapConstIterator it = _heaps.find(heapStart);
    if (it == _heaps.end()) {
      return false;
    }
    const Heap &heap = it->second;
    Offset offsetInHeap = address - heapStart;
    bool inHeapTail = (offsetInHeap >= heap._maxSize);
    bool heapTailMissing = false;
    bool heapTailMarkedReadable = false;
    if (offsetInHeap >= heap._size) {
      typename VirtualAddressMap<Offset>::const_iterator itMap =
          _addressMap.find(address);
      if (itMap == _addressMap.end()) {
        heapTailMissing = true;
        inHeapTail = true;
      } else {
        int flags = itMap.Flags();
        if ((flags & RangeAttributes::IS_WRITABLE) == 0) {
          inHeapTail = true;
          if ((flags & RangeAttributes::IS_READABLE) != 0) {
            heapTailMarkedReadable = true;
          }
        }
      }
    }
    Offset pastHeapHeader = heapStart + _heapHeaderSize;
    Offset pastArenaStruct = pastHeapHeader;
    if (pastHeapHeader == heap._arenaAddress) {
      pastArenaStruct += _arenaStructSize;
    }

    Commands::Output &output = context.GetOutput();
    if (showAddresses) {
      if (inHeapTail) {
        output << "Address 0x" << std::hex << address << " is in the heap tail"
                                                         " of the heap at 0x"
               << heapStart << ".\n";
      } else {
        output << "Address 0x" << std::hex << address << " is at offset 0x"
               << offsetInHeap << " of the heap at 0x" << heapStart << ".\n";
        if (address < pastHeapHeader) {
          output << "It is part of the heap header.\n";
        } else if (address < pastArenaStruct) {
          output << "It is at offset 0x" << std::hex
                 << (address - pastHeapHeader) << " of the non-main arena at 0x"
                 << pastHeapHeader << ".\n";
        } else if (address < pastArenaStruct + sizeof(Offset)) {
          output << "It is in the prev size field for the libc chunk for the "
                    "first allocation\nin the heap.\n";
        } else {
          /*
           * Note that we expect the describer for allocations to describe any
           * address in an allocation, including what libc would consider to be
           * the prev size for a libc chunk on the doubly linked free list.  For
           * this reason, this describer only mentions the prev size entry for
           * the
           * first allocation.
           */
          output << "It is in the size/status field for the libc chunk for the "
                    "allocation\nat 0x"
                 << ((address + sizeof(Offset)) & ~(sizeof(Offset) - 1))
                 << ".\n";
        }
      }
    } else {
      if (inHeapTail) {
        output << "This is a heap tail for the heap at 0x" << std::hex
               << heapStart << ".\n";
      } else {
        output << "This is a libc malloc heap of current size 0x" << std::hex
               << heap._size << " and maximum size reached so far 0x"
               << heap._maxSize << ".\n";
      }
    }
    if (explain) {
      output << "This is ";
      if (showAddresses) {
        output << "in ";
      }
      if (inHeapTail) {
        output << "the heap tail for ";
      }
      if (heap._address == heap._arenaAddress) {
        output << "the first heap ";
      } else {
        output << "one of multiple heaps ";
      }
      output << "for the arena at " << std::hex << heap._arenaAddress << ".\n";
      if (inHeapTail) {
        if (heapTailMissing) {
          output << "The tail is not listed in the core but is inferred "
                    "based on the preceding heap.\n";
        } else {
          if (heapTailMarkedReadable) {
            output << "The tail is marked readable, likely due to a bug in "
                      "creation of the core.\n";
          }
        }
      }
    }
    return true;
  }

 protected:
  const VirtualAddressMap<Offset> &_addressMap;
  const Offset _maxHeapSize;
  const Offset _heapHeaderSize;
  const HeapMap &_heaps;
  const Offset _arenaStructSize;
};
}  // namespace LibcMalloc
}  // namespace chap
