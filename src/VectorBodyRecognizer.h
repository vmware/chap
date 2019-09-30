// Copyright (c) 2018,2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternRecognizer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class VectorBodyRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  VectorBodyRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "VectorBody") {}

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
            if (endUsed == allocationAddress) {
              /*
               * An element block for a deque could potentially match the
               * pattern if either the _M_start or _M_finish for the deque
               * had _M_cur and _M_first pointing to the start of the element
               * block.  We can further check whether the pointer after the 3
               * for the vector would make sense as an _M_node.
               */
              Offset candidateMNode =
                  reader.ReadOffset(anchor + 3 * sizeof(Offset), 0xbad);
              if ((candidateMNode & (sizeof(Offset) - 1)) == 0 &&
                  reader.ReadOffset(candidateMNode) == allocationAddress) {
                Offset otherElementBlock =
                    reader.ReadOffset(anchor + 5 * sizeof(Offset), 0xbad);
                if ((otherElementBlock & (sizeof(Offset) - 1)) == 0) {
                  Offset otherMNode =
                      reader.ReadOffset(anchor + 7 * sizeof(Offset), 0xbad);
                  if (((otherMNode & (sizeof(Offset) - 1)) == 0) &&
                      reader.ReadOffset(otherMNode, 0) == otherElementBlock) {
                    continue;
                  }
                }
                otherElementBlock =
                    reader.ReadOffset(anchor - 3 * sizeof(Offset), 0xbad);
                if ((otherElementBlock & (sizeof(Offset) - 1)) == 0) {
                  Offset otherMNode =
                      reader.ReadOffset(anchor - sizeof(Offset), 0xbad);
                  if (((otherMNode & (sizeof(Offset) - 1)) == 0) &&
                      reader.ReadOffset(otherMNode, 0) == otherElementBlock) {
                    continue;
                  }
                }
              }
            }
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
     * We don't care whether the allocation is unsigned because in theory
     * a vector could contain pointers to read-only memory and such a
     * pointer at the first position would look like a signature.
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

    if (pPastIncoming - pFirstIncoming >= 1000) {
      /*
       * It is highly unlikely to have that many references, real or false,
       * to a vector.   For now it is deemed better to fail to match a vector
       * in such a case than to incur the cost of checking all the references
       * for a possible match.
       */
      return false;
    }
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
          typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
          if (candidates[candidateIndex + 1] == allocationAddress &&
              candidateIndex + 1 < numCandidates) {
            /*
             * An element block for a deque could potentially match the
             * pattern if either the _M_start or _M_finish for the deque
             * had _M_cur and _M_first pointing to the start of the element
             * block.  We can further check whether the pointer after the 3
             * for the vector would make sense as an _M_node.
             */
            Offset candidateMNode = candidates[candidateIndex + 3];
            if ((candidateMNode & (sizeof(Offset) - 1)) == 0 &&
                reader.ReadOffset(candidateMNode, 0) == allocationAddress) {
              if (candidateIndex + 5 < numCandidates) {
                Offset otherElementBlock = candidates[candidateIndex + 5];
                if ((otherElementBlock & (sizeof(Offset) - 1)) == 0) {
                  Offset otherMNode = candidates[candidateIndex + 7];
                  if (((otherMNode & (sizeof(Offset) - 1)) == 0) &&
                      reader.ReadOffset(otherMNode, 0) == otherElementBlock) {
                    continue;
                  }
                }
              }
              if (candidateIndex >= 4) {
                Offset otherElementBlock = candidates[candidateIndex - 3];
                if ((otherElementBlock & (sizeof(Offset) - 1)) == 0) {
                  Offset otherMNode = candidates[candidateIndex - 1];
                  if (((otherMNode & (sizeof(Offset) - 1)) == 0) &&
                      reader.ReadOffset(otherMNode, 0) == otherElementBlock) {
                    continue;
                  }
                }
              }
            }
          }
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
