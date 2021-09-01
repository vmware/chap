// Copyright (c) 2019-2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class DequeMapDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  DequeMapDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "DequeMap") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern DequeMap.\n";
    std::vector<DequeInfo> deques;
    Offset allocationSize = allocation.Size();
    Offset allocationAddress = allocation.Address();
    Offset allocationLimit = allocationAddress + allocationSize;
    FindDeques(InStaticMemory, allocationAddress, allocationLimit,
               Base::_graph->GetStaticAnchors(index), deques);
    FindDeques(OnStack, allocationAddress, allocationLimit,
               Base::_graph->GetStackAnchors(index), deques);
    FindDeques(allocationAddress, allocationLimit, index, deques);
    if (deques.size() == 1) {
      const DequeInfo& dequeInfo = deques[0];
      Offset startMNode = dequeInfo._startMNode;
      Offset finishMNode = dequeInfo._finishMNode;
      if (startMNode <= finishMNode) {
        output << "Only [0x" << std::hex << startMNode << ", 0x"
               << (finishMNode + sizeof(Offset)) << ") is considered live.\n";
      }
    }
    if (explain) {
      std::string label;
      if (deques.size() == 1) {
        label.assign("The deque");

      } else {
        label.assign("One possible deque");
        output << "It is strange that there are multiple deque candidates.\n";
      }
      for (auto dequeInfo : deques) {
        switch (dequeInfo._locationType) {
          case InAllocation:
            output << label << " is at offset 0x" << std::hex
                   << dequeInfo._offsetInAllocation
                   << " in the allocation at 0x" << dequeInfo._address << ".\n";
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

  bool IsPlausibleDequeFor(const Offset* dequeImage, Offset mapAddress,
                           Offset mapLimit) const {
    if (dequeImage[0] != mapAddress) {
      return false;
    }

    Offset startMNode = dequeImage[5];
    if (startMNode < mapAddress) {
      return false;
    }

    Offset finishMNode = dequeImage[9];
    if (finishMNode < startMNode || finishMNode >= mapLimit) {
      return false;
    }

    if ((startMNode & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    if ((finishMNode & (sizeof(Offset) - 1)) != 0) {
      return false;
    }

    Offset maxEntries = dequeImage[1];
    if (maxEntries < (finishMNode - mapAddress) / sizeof(Offset) + 1) {
      return false;
    }
    if (maxEntries > (mapLimit - mapAddress) / sizeof(Offset)) {
      return false;
    }
    Offset startCur = dequeImage[2];
    Offset startFirst = dequeImage[3];
    if (startCur < startFirst) {
      return false;
    }
    Offset startLast = dequeImage[4];
    if (startCur >= startLast) {
      return false;
    }
    Offset finishCur = dequeImage[6];
    Offset finishFirst = dequeImage[7];
    Offset finishLast = dequeImage[8];
    if (finishMNode == startMNode) {
      if (startFirst != finishFirst || startLast != finishLast ||
          startCur > finishCur) {
        return false;
      }
    } else {
      if (finishCur == 0xbad || finishFirst == 0xbad || finishLast == 0xbad ||
          finishCur < finishFirst || finishCur >= finishLast) {
        return false;
      }
    }
    // TODO: possibly check that startMNode points to startFirst
    // TODO: possibly check that finishMNode points to finishFirst
    return true;
  }

  void FindDeques(LocationType locationType, Offset mapAddress, Offset mapLimit,
                  const std::vector<Offset>* anchors,
                  std::vector<DequeInfo>& deques) const {
    if (anchors != nullptr) {
      typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
      for (Offset anchor : *anchors) {
        const char* image;
        Offset numBytesFound =
            Base::_addressMap.FindMappedMemoryImage(anchor, &image);

        if (numBytesFound < 10 * sizeof(Offset)) {
          continue;
        }
        const Offset* dequeImage = (Offset*)(image);
        if (IsPlausibleDequeFor(dequeImage, mapAddress, mapLimit)) {
          deques.emplace_back(locationType, anchor, dequeImage[5],
                              dequeImage[9], 0);
        }
      }
    }
  }
  void FindDeques(Offset mapAddress, Offset mapLimit, AllocationIndex index,
                  std::vector<DequeInfo>& deques) const {
    const AllocationIndex* pFirstIncoming;
    const AllocationIndex* pPastIncoming;
    Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);

    for (const AllocationIndex* pNextIncoming = pFirstIncoming;
         pNextIncoming < pPastIncoming; pNextIncoming++) {
      const Allocation* incoming =
          Base::_directory.AllocationAt(*pNextIncoming);
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
        continue;
      }
      Offset numCandidates = (incomingSize / sizeof(Offset)) - 9;
      const Offset* candidates = (const Offset*)(image);
      for (size_t candidateIndex = 0; candidateIndex < numCandidates;
           ++candidateIndex) {
        if (IsPlausibleDequeFor(candidates + candidateIndex, mapAddress,
                                mapLimit)) {
          deques.emplace_back(
              InAllocation, incomingAddress, candidates[candidateIndex + 5],
              candidates[candidateIndex + 9], candidateIndex * sizeof(Offset));
        }
      }
    }
  }
};
}  // namespace chap
