// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternRecognizer.h"
#include "../ProcessImage.h"

namespace chap {
namespace Linux {
template <typename Offset>
class SSLRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  typedef typename Allocations::TagHolder<Offset>::TagIndex TagIndex;
  SSLRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "SSL"),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _tagIndex(~((TagIndex)(0))) {
    const OpenSSLAllocationsTagger<Offset>* tagger =
        processImage.GetOpenSSLAllocationsTagger();
    if (tagger != 0) {
      _tagIndex = tagger->GetSSLTagIndex();
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
      Commands::Output& output = context.GetOutput();
      output << "This allocation matches pattern SSL.\n";
      if (explain) {
        output << "Offset " << sizeof(Offset)
               << " points to what appears to be an "
                  " SSL_METHOD structure.\n";
      }
    }
    return false;
  }

 private:
  const Allocations::TagHolder<Offset>* _tagHolder;
  TagIndex _tagIndex;
};
}  // namespace Linux
}  // namespace chap
