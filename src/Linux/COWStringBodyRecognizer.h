// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/ContiguousImage.h"
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
  typedef typename Allocations::TagHolder<Offset>::TagIndex TagIndex;
  COWStringBodyRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "COWStringBody"),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _contiguousImage(*(processImage.GetAllocationFinder())),
        _tagIndex(~((TagIndex)(0))) {
    const COWStringAllocationsTagger<Offset>* tagger =
        processImage.GetCOWStringAllocationsTagger();
    if (tagger != 0) {
      _tagIndex = tagger->GetTagIndex();
    }
  }

  bool Matches(AllocationIndex index, const Allocation& /* allocation */,
               bool /* isUnsigned */) const {
    return (_tagHolder->GetTagIndex(index) == _tagIndex);
  }

  /*
  *If the address is matches any of the registered patterns, provide a
  *description for the address as belonging to that pattern
  *optionally with an additional explanation of why the address matches
  *the description.  Return true only if the allocation matches the
  *pattern.
  */
  virtual bool Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& /* allocation */,
                        bool /* isUnsigned */, bool explain) const {
    if (_tagHolder->GetTagIndex(index) == _tagIndex) {
      _contiguousImage.SetIndex(index);
      const Offset* asOffsets = _contiguousImage.FirstOffset();

      int numRefsMinus1 = *((int32_t*)(asOffsets + 2));

      Offset stringLength = asOffsets[0];
      const char* stringStart = (const char*)(asOffsets + 3);

      Commands::Output& output = context.GetOutput();
      output << "This allocation matches pattern COWStringBody.\n";
      output << "This has capacity " << std::dec << asOffsets[1]
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

      return true;
    }
    return false;
  }

 private:
  const Allocations::TagHolder<Offset>* _tagHolder;
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
  TagIndex _tagIndex;
};
}  // namespace Linux
}  // namespace chap
