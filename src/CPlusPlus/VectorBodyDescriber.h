// Copyright (c) 2018-2020,2022,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"
#include "VectorAllocationsTagger.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class VectorBodyDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  VectorBodyDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "VectorBody") {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool explain) const {
    Offset allocationSize = allocation.Size();
    Offset allocationAddress = allocation.Address();
    Offset allocationLimit = allocationAddress + allocationSize;

    const AllocationIndex* pFirstIncoming;
    const AllocationIndex* pPastIncoming;
    Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);

    std::vector<VectorInfo> vectors;
    for (const AllocationIndex* pNextIncoming = pFirstIncoming;
         pNextIncoming < pPastIncoming; pNextIncoming++) {
      const Allocation* incoming =
          Base::_directory.AllocationAt(*pNextIncoming);
      if (incoming == 0) {
        abort();
      }
      Offset incomingSize = incoming->Size();
      if (!incoming->IsUsed() || incoming->Size() < 3 * sizeof(Offset)) {
        continue;
      }
      Offset incomingAddress = incoming->Address();
      const char* image;
      Offset numBytesFound =
          Base::_addressMap.FindMappedMemoryImage(incomingAddress, &image);

      if (numBytesFound < incomingSize) {
        return;
      }
      Offset numCandidates = (incomingSize / sizeof(Offset)) - 2;
      const Offset* candidates = (const Offset*)(image);
      for (size_t candidateIndex = 0; candidateIndex < numCandidates;
           ++candidateIndex) {
        if ((candidates[candidateIndex] == allocationAddress) &&
            (candidates[candidateIndex + 1] >= allocationAddress) &&
            (candidates[candidateIndex + 2] >= candidates[candidateIndex + 1]) &&
            (candidates[candidateIndex + 2] > allocationAddress) &&
            (candidates[candidateIndex + 2] <= allocationLimit)) {
          vectors.emplace_back(
              InAllocation, incomingAddress,
              candidates[candidateIndex + 1] - allocationAddress,
              candidates[candidateIndex + 2] - allocationAddress,
              candidateIndex * sizeof(Offset));
        }
      }
    }

    FindVectors(InStaticMemory, allocationAddress, allocationLimit,
                Base::_graph->GetStaticAnchors(index), vectors);
    FindVectors(OnStack, allocationAddress, allocationLimit,
                Base::_graph->GetStackAnchors(index), vectors);

    if (vectors.empty()) {
      return;
    }

    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern VectorBody.\n";
    std::string label;
    size_t numVectorCandidates = vectors.size();
    Offset bytesUsed = vectors[0]._bytesUsed;
    Offset bytesUsable = vectors[0]._bytesUsable;
    bool keepJustOne = true;
    if (numVectorCandidates > 1) {
      /*
       * If there are multiple possible vectors and all but one
       * can be explained as pointers into the used part of the other,
       * pick the other.
       */
      for (size_t index = 1; index < numVectorCandidates; index++) {
        Offset usableForThisVector = vectors[index]._bytesUsable;
        if (bytesUsable == usableForThisVector) {
          keepJustOne = false;
          continue;
        }
        if (bytesUsable < usableForThisVector) {
          bytesUsable = usableForThisVector;
          bytesUsed = vectors[index]._bytesUsed;
          keepJustOne = true;
        }
      }

      if (keepJustOne) {
        for (const auto& vectorInfo : vectors) {
          Offset bytesUsableForThisOne = vectorInfo._bytesUsable;
          if ((bytesUsableForThisOne < bytesUsable) &&
              (bytesUsableForThisOne > bytesUsed)) {
            keepJustOne = false;
            break;
          }
        }
      }
    }
    if (keepJustOne) {
      output << "Only the first 0x" << bytesUsed
             << " bytes are considered live.\n";
      label.assign("The vector");
    } else {
      label.assign("One possible vector");
      output << "It is strange that there are multiple vector candidates.\n";
    }
    if (explain) {
      for (auto vectorInfo : vectors) {
        switch (vectorInfo._locationType) {
          case InAllocation:
            output << label << " is at offset 0x" << std::hex
                   << vectorInfo._offsetInAllocation
                   << " in the allocation at 0x" << vectorInfo._address
                   << ".\n";
            break;
          case InStaticMemory:
            output << label << " is at address 0x" << std::hex
                   << vectorInfo._address
                   << " in statically allocated memory.\n";
            break;
          case OnStack:
            output << label << " is at address 0x" << std::hex
                   << vectorInfo._address << " on the stack.\n";
            break;
        }
      }
    }
  }

 private:
  enum LocationType { InAllocation, InStaticMemory, OnStack };
  struct VectorInfo {
    VectorInfo(LocationType locationType, Offset address, Offset bytesUsed,
               Offset bytesUsable, Offset offsetInAllocation)
        : _locationType(locationType),
          _address(address),
          _bytesUsed(bytesUsed),
          _bytesUsable(bytesUsable),
          _offsetInAllocation(offsetInAllocation) {}
    LocationType _locationType;
    Offset _address;
    Offset _bytesUsed;
    Offset _bytesUsable;
    Offset _offsetInAllocation;
  };
  void FindVectors(LocationType locationType, Offset allocationAddress,
                   Offset allocationLimit, const std::vector<Offset>* anchors,
                   std::vector<VectorInfo>& vectors) const {
    if (anchors != nullptr) {
      typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
      for (Offset anchor : *anchors) {
        if (reader.ReadOffset(anchor, 0xbad) == allocationAddress) {
          Offset endUsed = reader.ReadOffset(anchor + sizeof(Offset), 0xbad);
          Offset endUsable =
              reader.ReadOffset(anchor + 2 * sizeof(Offset), 0xbad);
          if (endUsed >= allocationAddress && endUsable >= endUsed &&
              endUsable > allocationAddress && endUsable <= allocationLimit) {
            vectors.emplace_back(locationType, anchor,
                                 endUsed - allocationAddress,
                                 endUsable - allocationAddress, 0);
          }
        }
      }
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
