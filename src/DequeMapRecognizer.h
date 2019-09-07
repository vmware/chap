// Copyright (c) 2018,2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternRecognizer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class DequeMapRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  DequeMapRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "DequeMap") {}

  bool Matches(AllocationIndex index, const Allocation& allocation,
               bool isUnsigned) const {
    return Visit(nullptr, index, allocation, isUnsigned, false);
  }

  /*
  *If the address is matches any of the registered patterns, provide a
  *description for the address as belonging to that pattern
  *optionally with an additional explanation of why the address matches
  *the description.  Return true only if the allocation matches the
  *pattern.
  */
  virtual bool Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool isUnsigned,
                        bool explain) const {
    return Visit(&context, index, allocation, isUnsigned, explain);
  }

 private:
  enum LocationType { InAllocation, InStaticMemory, OnStack };
  struct DequeInfo {
    DequeInfo(LocationType locationType, Offset address, Offset startMNode,
              Offset finishMNode, Offset offsetInAllocation)
        : _locationType(locationType),
          _address(address),
          _startMNode(startMNode),
          _finishMNode(finishMNode),
          _offsetInAllocation(offsetInAllocation) {}
    LocationType _locationType;
    Offset _address;
    Offset _startMNode;
    Offset _finishMNode;
    Offset _offsetInAllocation;
  };
  void FindDeques(LocationType locationType, Offset allocationAddress,
                  Offset allocationLimit, const std::vector<Offset>* anchors,
                  std::vector<DequeInfo>& deques) const {
    if (anchors != nullptr) {
      Offset maxMaxEntries =
          (allocationLimit - allocationAddress) / sizeof(Offset);
      Offset minMaxEntries = (maxMaxEntries <= 9) ? 4 : (maxMaxEntries - 5);

      typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
      for (Offset anchor : *anchors) {
        if (reader.ReadOffset(anchor, 0xbad) != allocationAddress) {
          continue;
        }
        Offset maxEntries =
            reader.ReadOffset(anchor + sizeof(Offset), 0xbadbad);
        if (maxEntries == 0xbadbad || maxEntries > maxMaxEntries ||
            maxEntries < minMaxEntries) {
          continue;
        }
        Offset liveAreaLimit = allocationAddress + maxEntries * sizeof(Offset);
        Offset startMNode =
            reader.ReadOffset(anchor + 5 * sizeof(Offset), 0xbad);
        if ((startMNode & (sizeof(Offset) - 1)) != 0 ||
            startMNode < allocationAddress || startMNode >= liveAreaLimit) {
          continue;
        }
        Offset finishMNode =
            reader.ReadOffset(anchor + 9 * sizeof(Offset), 0xbad);
        if (finishMNode != startMNode &&
            ((finishMNode & (sizeof(Offset) - 1)) != 0 ||
             finishMNode < allocationAddress || finishMNode >= liveAreaLimit)) {
          continue;
        }

        Offset startCur = reader.ReadOffset(anchor + 2 * sizeof(Offset), 0xbad);
        if (startCur == 0xbad) {
          continue;
        }
        Offset startFirst =
            reader.ReadOffset(anchor + 3 * sizeof(Offset), 0xbad);
        if (startFirst == 0xbad || startCur < startFirst) {
          continue;
        }
        Offset startLast =
            reader.ReadOffset(anchor + 4 * sizeof(Offset), 0xbad);
        if (startLast == 0xbad || startCur >= startLast) {
          continue;
        }
        Offset finishCur =
            reader.ReadOffset(anchor + 6 * sizeof(Offset), 0xbad);
        Offset finishFirst =
            reader.ReadOffset(anchor + 7 * sizeof(Offset), 0xbad);
        Offset finishLast =
            reader.ReadOffset(anchor + 8 * sizeof(Offset), 0xbad);
        if (finishMNode == startMNode) {
          if (startFirst != finishFirst || startLast != finishLast ||
              startCur > finishCur) {
            continue;
          }
        } else {
          if (finishCur == 0xbad || finishFirst == 0xbad ||
              finishLast == 0xbad || finishCur < finishFirst ||
              finishCur >= finishLast) {
            continue;
          }
        }

        if (reader.ReadOffset(startMNode, 0xbad) != startFirst) {
          continue;
        }
        // TODO: check that startFirst starts an allocation of 0x200+ bytes 
        if (startMNode != finishMNode) {
          if (reader.ReadOffset(finishMNode, 0xbad) != finishFirst) {
            continue;
          }
          // TODO: check that finishFirst starts an allocation of 0x200+ bytes
        }
        deques.emplace_back(locationType, anchor, startMNode, finishMNode, 0);
      }
    }
  }
  virtual bool Visit(Commands::Context* context, AllocationIndex index,
                     const Allocation& allocation, bool /* isUnsigned */,
                     bool explain) const {
    /*
     * We can't look at the signature area of an allocation possibly used
     * for a map for a deque, because the deque often leaves the head and tail
     * of the map uninitialized and depends on _M_start._M_node and
     * _M_finish._M_node to understand the limits of the live area of the map.
     */
    if (!allocation.IsUsed()) {
      /*
       * Given that a vector body is recognized on how it is referenced,
       * rather than on the contents of the allocation, there is no point
       * trying to recognize a freed vector body.
       */
      return false;
    }
    Offset allocationSize = allocation.Size();
    Offset allocationAddress = allocation.Address();
    Offset allocationLimit = allocationAddress + allocationSize;

    const AllocationIndex* pFirstIncoming;
    const AllocationIndex* pPastIncoming;
    Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);

    std::vector<DequeInfo> deques;
    typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
    for (const AllocationIndex* pNextIncoming = pFirstIncoming;
         pNextIncoming < pPastIncoming; pNextIncoming++) {
      const Allocation* incoming = Base::_finder->AllocationAt(*pNextIncoming);
      if (incoming == 0) {
        abort();
      }
      Offset incomingSize = incoming->Size();
      if (!incoming->IsUsed() || incoming->Size() < 10 * sizeof(Offset)) {
        continue;
      }
      Offset incomingAddress = incoming->Address();
      const char* image;
      Offset numBytesFound =
          Base::_addressMap.FindMappedMemoryImage(incomingAddress, &image);

      if (numBytesFound < incomingSize) {
        return false;
      }
      Offset maxMaxEntries =
          (allocationLimit - allocationAddress) / sizeof(Offset);
      Offset minMaxEntries = (maxMaxEntries <= 9) ? 4 : (maxMaxEntries - 5);
      Offset numCandidates = (incomingSize / sizeof(Offset)) - 9;
      const Offset* candidates = (const Offset*)(image);
      for (size_t candidateIndex = 0; candidateIndex < numCandidates;
           ++candidateIndex) {
        if (candidates[candidateIndex] != allocationAddress) {
          continue;
        }
        Offset maxEntries = candidates[candidateIndex + 1];
        if (maxEntries > maxMaxEntries || maxEntries < minMaxEntries) {
          continue;
        }
        Offset liveAreaLimit = allocationAddress + maxEntries * sizeof(Offset);
        Offset startMNode = candidates[candidateIndex + 5];
        if ((startMNode & (sizeof(Offset) - 1)) != 0 ||
            startMNode < allocationAddress || startMNode >= liveAreaLimit) {
          continue;
        }
        Offset finishMNode = candidates[candidateIndex + 9];
        if (finishMNode != startMNode &&
            ((finishMNode & (sizeof(Offset) - 1)) != 0 ||
             finishMNode < allocationAddress || finishMNode >= liveAreaLimit)) {
          continue;
        }

        Offset startCur = candidates[candidateIndex + 2];
        Offset startFirst = candidates[candidateIndex + 3];
        if (startCur < startFirst) {
          continue;
        }
        Offset startLast = candidates[candidateIndex + 4];
        if (startCur >= startLast) {
          continue;
        }
        Offset finishCur = candidates[candidateIndex + 6];
        Offset finishFirst = candidates[candidateIndex + 7];
        Offset finishLast = candidates[candidateIndex + 8];
        if (finishMNode == startMNode) {
          if (startFirst != finishFirst || startLast != finishLast ||
              startCur > finishCur) {
            continue;
          }
        } else {
          if (finishCur < finishFirst || finishCur >= finishLast) {
            continue;
          }
        }

        if (reader.ReadOffset(startMNode, 0xbad) != startFirst) {
          continue;
        }
        // TODO: check that startFirst starts an allocation of 0x200+ bytes 
        if (startMNode != finishMNode) {
          if (reader.ReadOffset(finishMNode, 0xbad) != finishFirst) {
            continue;
          }
          // TODO: check that finishFirst starts an allocation of 0x200+ bytes
        }
        deques.emplace_back(InAllocation, incomingAddress, startMNode,
                            finishMNode, candidateIndex * sizeof(Offset));
      }
    }

    FindDeques(InStaticMemory, allocationAddress, allocationLimit,
               Base::_graph->GetStaticAnchors(index), deques);
    FindDeques(OnStack, allocationAddress, allocationLimit,
               Base::_graph->GetStackAnchors(index), deques);

    if (deques.empty()) {
      return false;
    }

    if (context != 0) {
      Commands::Output& output = context->GetOutput();
      output << "This allocation matches pattern DequeMap.\n";
      std::string label;
      if (deques.size() == 1) {
        const DequeInfo& dequeInfo = deques[0];
        Offset startMNode = dequeInfo._startMNode;
        Offset finishMNode = dequeInfo._finishMNode;
        if (startMNode <= finishMNode) {
          output << "Only [0x" << std::hex << startMNode << ", 0x"
                 << (finishMNode + sizeof(Offset)) << ") is considered live.\n";
        }
        label.assign("The deque");
      } else {
        label.assign("One possible deque");
        output << "It is strange that there are multiple deque candidates.\n";
      }
      if (explain) {
        for (auto dequeInfo : deques) {
          switch (dequeInfo._locationType) {
            case InAllocation:
              output << label << " is at offset 0x" << std::hex
                     << dequeInfo._offsetInAllocation
                     << " in the allocation at 0x" << dequeInfo._address
                     << ".\n";
              break;
            case InStaticMemory:
              output << label << " is at address 0x" << std::hex
                     << dequeInfo._address
                     << " in statically allocated memory.\n";
              break;
            case OnStack:
              output << label << " is at address 0x" << std::hex
                     << dequeInfo._address << " on the stack.\n";
              break;
          }
        }
      }
    }

    return true;
  }
};
}  // namespace chap
