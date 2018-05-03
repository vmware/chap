// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternRecognizer.h"
#include "../ProcessImage.h"

namespace chap {
namespace Linux {
template <typename Offset>
class SSL_CTXRecognizer : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  SSL_CTXRecognizer(const ProcessImage<Offset>* processImage)
      : Allocations::PatternRecognizer<Offset>(processImage, "SSL_CTX") {}

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
  virtual bool Visit(Commands::Context* context, AllocationIndex,
                     const Allocation& allocation, bool isUnsigned,
                     bool explain) const {
    /*
     * An SSL_CTX is necessarily signed because the first quadword points
     * to a const SSL_METHOD structure and the definition of a signature in
     * chap is a pointer to memory that is not readable.  The reason we
     * have a pattern here, even though in the chap sense the allocation is
     * considered "signed" is because the signature varies and is determined
     * by the location of the SSL_METHOD structure.
     */

    if (isUnsigned) {
      return false;
    }

    /*
     * An SSL_CTX is fairly large.  This rules out many candidates but is
     * intended only to improve performance and is not at all an exact check.
     */

    Offset allocationSize = allocation.Size();
    if (allocationSize < 0x40 * sizeof(Offset)) {
      return false;
    }

    const char* allocationImage;
    Offset allocationAddress = allocation.Address();
    Offset numBytesFound = Base::_addressMap->FindMappedMemoryImage(
        allocationAddress, &allocationImage);

    if (numBytesFound < allocationSize) {
      return false;
    }

    Offset sslMethodCandidate = *((Offset*)(allocationImage));
    std::string moduleName;
    Offset rangeBase;
    Offset rangeSize;
    Offset fileOffset;
    Offset relativeVirtualAddress;
    if ((!Base::_moduleDirectory->Find(sslMethodCandidate, moduleName,
                                       rangeBase, rangeSize, fileOffset,
                                       relativeVirtualAddress)) ||
        (moduleName.find("libssl") == std::string::npos)) {
      return false;
    }

    const char* sslMethodImage;
    numBytesFound = Base::_addressMap->FindMappedMemoryImage(sslMethodCandidate,
                                                             &sslMethodImage);

    if (numBytesFound < 10 * sizeof(Offset)) {
      return false;
    }

    Offset* methodPointers = (Offset*)(sslMethodImage + sizeof(Offset));
    std::string methodModuleName;
    if ((!Base::_moduleDirectory->Find(methodPointers[0], methodModuleName,
                                       rangeBase, rangeSize, fileOffset,
                                       relativeVirtualAddress)) ||
        (methodModuleName != moduleName)) {
      return false;
    }

    typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
    typename VirtualAddressMap<Offset>::const_iterator it =
        Base::_addressMap->find(methodPointers[0]);
    if ((it == Base::_addressMap->end()) ||
        ((it.Flags() &
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE |
           RangeAttributes::IS_EXECUTABLE)) !=
         (RangeAttributes::IS_READABLE | RangeAttributes::IS_EXECUTABLE))) {
      return false;
    }

    Offset rangeLimit = rangeBase + rangeSize;
    for (size_t i = 1; i < 5; i++) {
      Offset methodPointer = methodPointers[i];
      if (methodPointer < rangeBase || methodPointer >= rangeLimit) {
        return false;
      }
    }

    if (context != 0) {
      Commands::Output& output = context->GetOutput();
      output << "This allocation matches pattern SSL_CTX.\n";
      if (explain) {
        output << "The first pointer points to what appears to be an "
                  " SSL_METHOD structure.\n";
      }
    }

    return true;
  }
};
}  // namespace Linux
}  // namespace chap
