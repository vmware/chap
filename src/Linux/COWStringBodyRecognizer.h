// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternRecognizer.h"
#include "../ProcessImage.h"

namespace chap {
namespace Linux {
template <typename Offset>
class COWStringBodyRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  COWStringBodyRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "COWStringBody") {}

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
  /*
   * Count things that look like instances of std::string, which should
   * reference
   * the start of the c-string in the candidate COWStringBody.
   */
  int CountStdStrings(Offset cStringAddress, const std::vector<Offset>* anchors,
                      int limit) const {
    int numStrings = 0;
    if (anchors != nullptr) {
      typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
      for (Offset anchor : *anchors) {
        if (reader.ReadOffset(anchor, 0xbad) == cStringAddress) {
          if (++numStrings == limit) {
            break;
          }
        }
      }
    }
    return numStrings;
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
    if (allocationSize < 3 * sizeof(Offset) + 1) {
      return false;
    }

    Offset allocationAddress = allocation.Address();
    const char* image;
    Offset numBytesFound =
        Base::_addressMap.FindMappedMemoryImage(allocationAddress, &image);

    if (numBytesFound < allocationSize) {
      return false;
    }
    Offset cStringAddress = allocationAddress + 3 * sizeof(Offset);

    Offset maxCapacity = allocationSize - (Offset)(3 * sizeof(Offset) + 1);
    Offset capacity = ((Offset*)(image))[1];
    if (capacity == 0 || capacity > maxCapacity ||
        (maxCapacity > 5 * sizeof(Offset) && capacity < (maxCapacity >> 1))) {
      /*
       * The assumption is that for a string body with 0 capacity, as
       * opposed to 0 length, one would just use the existing static.
       * The capacity cannot actually exceed what would fit in the remainder
       * of the buffer and it is suspect if malloc gave back a much bigger
       * buffer than was needed to support the given capacity.
       */
      return false;
    }

    int numRefsMinus1 = *((int32_t*)(image + (2 * sizeof(Offset))));

    if (numRefsMinus1 < 0) {
      /*
       * The value in the reference count area, which is off by 1 so that
       * a 0 represents 1 reference, should never be negative.
       */
      return false;
    }

    Offset stringLength = ((Offset*)(image))[0];
    if (stringLength > capacity) {
      return false;
    }

    const char* stringStart = image + 3 * sizeof(Offset);

    size_t measuredLength = strnlen(stringStart, (size_t)stringLength + 1);
    if (measuredLength != (size_t)(stringLength)) {
      return false;
    }

    int numStdStrings = 0;
    int limit = 100;

    const AllocationIndex* pFirstIncoming;
    const AllocationIndex* pPastIncoming;
    Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);
    for (const AllocationIndex* pNextIncoming = pFirstIncoming;
         pNextIncoming < pPastIncoming; pNextIncoming++) {
      const Allocation* incoming = Base::_finder->AllocationAt(*pNextIncoming);
      if (incoming == 0) {
        abort();
      }
      Offset incomingSize = incoming->Size();
      if (!incoming->IsUsed() || incoming->Size() < sizeof(Offset)) {
        continue;
      }
      Offset incomingAddress = incoming->Address();
      const char* incomingImage;
      Offset numBytesFound = Base::_addressMap.FindMappedMemoryImage(
          incomingAddress, &incomingImage);

      if (numBytesFound < incomingSize) {
        return false;
      }
      Offset numCandidates = incomingSize / sizeof(Offset);
      const Offset* candidates = (const Offset*)(incomingImage);
      for (size_t candidateIndex = 0; candidateIndex < numCandidates;
           ++candidateIndex) {
        if (candidates[candidateIndex] == cStringAddress) {
          if (++numStdStrings == limit) {
            break;
          }
        }
      }
    }
    if (numStdStrings < limit) {
      numStdStrings +=
          CountStdStrings(cStringAddress, Base::_graph->GetStaticAnchors(index),
                          limit - numStdStrings);
      if (numStdStrings < limit) {
        numStdStrings += CountStdStrings(cStringAddress,
                                         Base::_graph->GetStackAnchors(index),
                                         limit - numStdStrings);
      }
    }

    if (numStdStrings < limit && numRefsMinus1 > numStdStrings - 1) {
      /*
       * Some of the references have not been accounted for.  We give
       * a bit of wiggle room here for the case that the string seems well
       * formed and is referenced at the correct offset a few times, to
       * allow for loss of references by corruption or slicing or zero-filled
       * cores.
       */
      if (stringLength == 0) {
        /*
         * Don't be as forgiving in the case of an empty string
         * because that is often done by a reference to statically allocated
         * memory and basically the cases when empty strings take dynamically
         * allocated COWString bodies, such as when a non-empty string is
         * cleared, are less common.
         */
        return false;
      }
      if (numStdStrings * 4 < (numRefsMinus1 + 1) * 3) {
        return false;
      }
    }

    if (context != 0) {
      Commands::Output& output = context->GetOutput();
      output << "This allocation matches pattern COWStringBody.\n";
      output << "This has capacity " << std::dec << capacity
             << ", reference count " << (numRefsMinus1 + 1)
             << " and a string of size " << stringLength;
      if (explain || stringLength < 77) {
        output << " containing\n\"" << stringStart << "\".\n";
      } else {
        output << " starting with\n\"" << std::string(stringStart, 77)
               << "\",\n";
      }
      if (explain) {
        /*
         * TODO: separate any pointers to the c-string part (which are
         * valid references, from pointers to elsewhere in the string,
         * which are not.  Check both incoming references from free
         * allocations and anchors in that way and the sum of the counts
         * should match the reference count associated with the string body.
         * If the sum is smaller, this means that either one of the references
         * was corrupted or the containing object was sliced on destruction.
         * This is optional, as COW strings are not even present in recent
         * C++ libraries and it is better to get other pattern recognizers
         * working first.
         */
      }
    }

    return true;
  }
};
}  // namespace Linux
}  // namespace chap
