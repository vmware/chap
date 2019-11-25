// Copyright (c) 2018,2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternRecognizer.h"
#include "ProcessImage.h"
#include "VectorAllocationsTagger.h"

namespace chap {
template <typename Offset>
class VectorBodyRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  typedef typename Allocations::TagHolder<Offset>::TagIndex TagIndex;
  VectorBodyRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "VectorBody"),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _tagIndex(~((TagIndex)(0))) {
    const VectorAllocationsTagger<Offset>* tagger =
        processImage.GetVectorAllocationsTagger();
    if (tagger != nullptr) {
      _tagIndex = tagger->GetTagIndex();
    }
  }

  bool Matches(AllocationIndex index, const Allocation& /* allocation */,
               bool /* isUnsigned */) const {
    return _tagHolder->GetTagIndex(index) == _tagIndex;
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
    bool matches = (_tagHolder->GetTagIndex(index) == _tagIndex);
    if (matches) {
      if (!Visit(&context, index, allocation, isUnsigned, explain)) {
        std::cerr << "Warning: describer for %VectorBody doesn't recognize "
                     "pre-tagged allocation\nat 0x"
                  << std::hex << allocation.Address() << "\n";
      }
    }
    return matches;
  }

 private:
  enum LocationType { InAllocation, InStaticMemory, OnStack };
  struct VectorInfo {
    VectorInfo(LocationType locationType, Offset address, Offset bytesUsed,
               Offset offsetInAllocation)
        : _locationType(locationType),
          _address(address),
          _bytesUsed(bytesUsed),
          _offsetInAllocation(offsetInAllocation) {}
    LocationType _locationType;
    Offset _address;
    Offset _bytesUsed;
    Offset _offsetInAllocation;
  };
  const Allocations::TagHolder<Offset>* _tagHolder;
  TagIndex _tagIndex;
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
                                 endUsed - allocationAddress, 0);
          }
        }
      }
    }
  }
  virtual bool Visit(Commands::Context* context, AllocationIndex index,
                     const Allocation& allocation, bool /* isUnsigned */,
                     bool explain) const {
    /*
     * Now that pre-tagging is done, the recognizer counts on the pre-tagger
     * to actually check for a match and the following is just to find
     * the referencing vector for the purpose of the "describe" or "explain"
     * command.
     */
    Offset allocationSize = allocation.Size();
    Offset allocationAddress = allocation.Address();
    Offset allocationLimit = allocationAddress + allocationSize;

    const AllocationIndex* pFirstIncoming;
    const AllocationIndex* pPastIncoming;
    Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);

    std::vector<VectorInfo> vectors;
    for (const AllocationIndex* pNextIncoming = pFirstIncoming;
         pNextIncoming < pPastIncoming; pNextIncoming++) {
      const Allocation* incoming = Base::_finder->AllocationAt(*pNextIncoming);
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
        return false;
      }
      Offset numCandidates = (incomingSize / sizeof(Offset)) - 2;
      const Offset* candidates = (const Offset*)(image);
      for (size_t candidateIndex = 0; candidateIndex < numCandidates;
           ++candidateIndex) {
        if (candidates[candidateIndex] == allocationAddress &&
            candidates[candidateIndex + 1] >= allocationAddress &&
            candidates[candidateIndex + 2] >= candidates[candidateIndex + 1] &&
            candidates[candidateIndex + 2] > allocationAddress &&
            candidates[candidateIndex + 2] <= allocationLimit) {
          vectors.emplace_back(
              InAllocation, incomingAddress,
              candidates[candidateIndex + 1] - allocationAddress,
              candidateIndex * sizeof(Offset));
        }
      }
    }

    FindVectors(InStaticMemory, allocationAddress, allocationLimit,
                Base::_graph->GetStaticAnchors(index), vectors);
    FindVectors(OnStack, allocationAddress, allocationLimit,
                Base::_graph->GetStackAnchors(index), vectors);

    if (vectors.empty()) {
      return false;
    }

    if (context != 0) {
      Commands::Output& output = context->GetOutput();
      output << "This allocation matches pattern VectorBody.\n";
      std::string label;
      if (vectors.size() == 1) {
        output << "Only the first 0x" << std::hex << vectors.begin()->_bytesUsed
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

    return true;
  }
};
}  // namespace chap
