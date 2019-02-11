// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternRecognizer.h"
#include "../ProcessImage.h"

namespace chap {
namespace Linux {
template <typename Offset>
class LongStringRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  LongStringRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "LongString") {}

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
  struct StringInfo {
    StringInfo(LocationType locationType, Offset address, Offset capacity,
               Offset offsetInAllocation)
        : _locationType(locationType),
          _address(address),
          _capacity(capacity),
          _offsetInAllocation(offsetInAllocation) {}
    LocationType _locationType;
    Offset _address;
    Offset _capacity;
    Offset _offsetInAllocation;
  };
  void FindStrings(LocationType locationType, Offset allocationAddress,
                   Offset stringLength, Offset allocationSize,
                   const std::vector<Offset>* anchors,
                   std::vector<StringInfo>& strings) const {
    if (anchors != 0) {
      typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
      for (Offset anchor : *anchors) {
        if (reader.ReadOffset(anchor, 0xbad) == allocationAddress) {
          Offset stringLengthCandidate =
              reader.ReadOffset(anchor + sizeof(Offset), 0xbad);
          if (stringLengthCandidate == stringLength) {
            Offset capacity =
                reader.ReadOffset(anchor + 2 * sizeof(Offset), 0xbad);
            if (capacity >= stringLength && capacity < allocationSize) {
              strings.emplace_back(locationType, anchor, capacity, 0);
            }
          }
        }
      }
    }
  }
  virtual bool Visit(Commands::Context* context, AllocationIndex index,
                     const Allocation& allocation, bool isUnsigned,
                     bool explain) const {
    if (!isUnsigned) {
      /*
       * For now, assume that the size field of a string will never match
       * a value that would be interpreted as a signature.  This is just
       * as a performance enhancement and it can be removed if it is
       * determined to introduce any false negatives.
       */
      return false;
    }
    Offset allocationSize = allocation.Size();
    Offset allocationAddress = allocation.Address();

    const char* allocationImage;
    Offset numBytesFound = Base::_addressMap.FindMappedMemoryImage(
        allocationAddress, &allocationImage);

    if (numBytesFound < allocationSize) {
      return false;
    }
    size_t stringLength = 0;
    while (allocationImage[stringLength] != '\000') {
      if (++stringLength == allocationSize) {
        return false;
      }
    }
    if (stringLength < 0x10) {
      return false;
    }

    const AllocationIndex* pFirstIncoming;
    const AllocationIndex* pPastIncoming;
    Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);

    std::vector<StringInfo> strings;
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
      const char* incomingImage;
      Offset numBytesFound = Base::_addressMap.FindMappedMemoryImage(
          incomingAddress, &incomingImage);

      if (numBytesFound < incomingSize) {
        return false;
      }
      Offset numCandidates = (incomingSize / sizeof(Offset)) - 2;
      const Offset* candidates = (const Offset*)(incomingImage);
      for (size_t candidateIndex = 0; candidateIndex < numCandidates;
           ++candidateIndex) {
        Offset capacity = candidates[candidateIndex + 2];
        if (candidates[candidateIndex] == allocationAddress &&
            candidates[candidateIndex + 1] == stringLength &&
            capacity >= stringLength && capacity < allocationSize) {
          strings.emplace_back(InAllocation, incomingAddress, capacity,
                               candidateIndex * sizeof(Offset));
        }
      }
    }

    FindStrings(InStaticMemory, allocationAddress, stringLength, allocationSize,
                Base::_graph->GetStaticAnchors(index), strings);
    FindStrings(OnStack, allocationAddress, stringLength, allocationSize,
                Base::_graph->GetStackAnchors(index), strings);

    if (strings.empty()) {
      return false;
    }

    if (context != 0) {
      Commands::Output& output = context->GetOutput();
      output << "This allocation matches pattern LongString.\n";
      output << "The string has 0x" << std::hex << stringLength << " bytes, ";
      if (explain || stringLength < 77) {
        output << "containing\n\"" << allocationImage << "\".\n";
      } else {
        output << "starting with\n\"" << std::string(allocationImage, 77)
               << "\".\n";
      }
      std::string label;
      if (strings.size() == 1) {
        label.assign("The referencing std::string");
        output << "The capacity is considered to be 0x" << std::hex
               << strings.begin()->_capacity << ".\n";
      } else {
        label.assign("One possible referencing std::string");
        output << "It is strange that there are multiple string candidates.\n";
      }
      if (explain) {
        for (auto stringInfo : strings) {
          switch (stringInfo._locationType) {
            case InAllocation:
              output << label << " is at offset 0x" << std::hex
                     << stringInfo._offsetInAllocation
                     << " in the allocation at 0x" << stringInfo._address
                     << ".\n";
              break;
            case InStaticMemory:
              output << label << " is at address 0x" << std::hex
                     << stringInfo._address
                     << " in statically allocated memory.\n";
              break;
            case OnStack:
              output << label << " is at address 0x" << std::hex
                     << stringInfo._address << " on the stack.\n";
              break;
          }
        }
      }
    }

    return true;
  }
};
}  // namespace Linux
}  // namespace chap
