// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
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
  COWStringBodyRecognizer(const ProcessImage<Offset>* processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "COWStringBody") {}

  bool Matches(AllocationIndex index, const Allocation& allocation,
               bool isUnsigned) const {
    return Visit((Commands::Context*)(0), index, allocation, isUnsigned, false);
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

    const char* image;
    Offset numBytesFound =
        Base::_addressMap->FindMappedMemoryImage(allocation.Address(), &image);

    if (numBytesFound < allocationSize) {
      return false;
    }

    Offset maxCapacity = allocationSize - (Offset)(3 * sizeof(Offset) - 1);
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

    Offset stringLength = ((Offset*)(image))[0];
    if (stringLength > capacity) {
      return false;
    }

    const AllocationIndex* pFirstIncoming;
    const AllocationIndex* pPastIncoming;
    Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);

    /*
     * Note that after the following statement, numRefsTotal will include
     * any references from either used or free allocations.
     */

    int numRefsTotal = pPastIncoming - pFirstIncoming;
    int numRefsMinus1 = *((int32_t*)(image + (2 * sizeof(Offset))));

    if (numRefsMinus1 < 0 || numRefsMinus1 > numRefsTotal + 5) {
      /*
       * This will still allow the case where a small number of references
       * have been dropped without decrementing the reference count, as
       * might happen in a slice-on-free of the object containing the
       * string, but will not identify the case where there have been
       * many such occurrences.
       */
      return false;
    }

    const char* stringStart = image + 3 * sizeof(Offset);

    size_t measuredLength = strnlen(stringStart, (size_t)stringLength + 1);
    if (measuredLength != (size_t)(stringLength)) {
      return false;
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
