// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternRecognizer.h"
#include "../ProcessImage.h"

namespace chap {
namespace Linux {
template <typename Offset>
class PyDictKeysObjectRecognizer
    : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  PyDictKeysObjectRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage,
                                               "PyDictKeysObject"),
        _stringTypeObj(0) {}

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
  /* The following two fields are mutable because they are calculated lazily.
   */
  mutable std::set<Offset> _methods;
  mutable Offset _stringTypeObj;
  virtual bool Visit(Commands::Context* context, AllocationIndex,
                     const Allocation& allocation, bool isUnsigned,
                     bool explain) const {
    /*
     * A PyDictKeysObject is necessarily unsigned because the first quadword
     * is always 0.
     */

    if (!isUnsigned) {
      return false;
    }

    /*
     * A PyDictKeysObject is fairly large has a minimum size.
     */

    Offset allocationSize = allocation.Size();
    if (allocationSize < 8 * sizeof(Offset)) {
      return false;
    }

    const char* allocationImage;
    Offset allocationAddress = allocation.Address();
    Offset numBytesFound = Base::_addressMap.FindMappedMemoryImage(
        allocationAddress, &allocationImage);

    if (numBytesFound < allocationSize) {
      return false;
    }

    /*
     * A PyDictKeysObject is uniquely referenced by a PyDictObject.
     */

    if (((Offset*)(allocationImage))[0] != 1) {
      return false;
    }

    /*
     * The number of slots in a PyDictKeysObject is always a power of 2.
     */

    Offset numSlots = ((Offset*)(allocationImage))[1];
    if ((numSlots ^ (numSlots - 1)) != 2 * numSlots - 1) {
      return false;
    }

    /*
     * Slots are triples and there is an additional fixed overhead.
     */

    if (allocationSize < sizeof(Offset) * (5 + numSlots * 3)) {
      return false;
    }

    Offset methodCandidate = ((Offset*)(allocationImage))[2];
    if (_methods.find(methodCandidate) == _methods.end()) {
      std::string moduleName;
      Offset rangeBase;
      Offset rangeSize;
      Offset relativeVirtualAddress;
      if ((!Base::_moduleDirectory.Find(methodCandidate, moduleName, rangeBase,
                                        rangeSize, relativeVirtualAddress)) ||
          (moduleName.find("python") == std::string::npos)) {
        return false;
      }

      typedef
          typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
      typename VirtualAddressMap<Offset>::const_iterator it =
          Base::_addressMap.find(methodCandidate);
      if ((it == Base::_addressMap.end()) ||
          ((it.Flags() &
            (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE |
             RangeAttributes::IS_EXECUTABLE)) !=
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_EXECUTABLE))) {
        return false;
      }
      _methods.insert(methodCandidate);
    }

    if (context != 0) {
      Commands::Output& output = context->GetOutput();
      output << "This allocation matches pattern PyDictKeysObject.\n";
      for (Offset slot = 0; slot < numSlots; slot++) {
        Offset key = ((Offset*)(allocationImage))[5 + slot * 3];
        Offset value = ((Offset*)(allocationImage))[6 + slot * 3];
        if (key == 0 || value == 0) {
          continue;
        }
        const char* keyImage;
        Offset numKeyBytesFound =
            Base::_addressMap.FindMappedMemoryImage(key, &keyImage);

        if (numKeyBytesFound < 7 * sizeof(Offset)) {
          continue;
        }
        Offset keyType = ((Offset*)(keyImage))[1];

        const char* valueImage;
        Offset numValueBytesFound =
            Base::_addressMap.FindMappedMemoryImage(value, &valueImage);

        if (numValueBytesFound < 7 * sizeof(Offset)) {
          continue;
        }
        Offset valueType = ((Offset*)(valueImage))[1];

        if (keyType != valueType) {
          /*
           * For the purposes of this current way of describing, we are
           * only interested in the key/value pairs where both the keys and
           * values are strings.  At some point when the "annotate" command
           * is implemented, this code may be dropped because the user will
           * be able to use that command to find the needed pairs.
           */
          continue;
        }

        if (_stringTypeObj == 0) {
          if (strlen(keyImage + 6 * sizeof(Offset)) !=
              ((Offset*)(keyImage))[2]) {
            continue;
          }
          _stringTypeObj = keyType;
        } else {
          if (keyType != _stringTypeObj || valueType != _stringTypeObj) {
            continue;
          }
        }
        output << "\"" << (keyImage + 6 * sizeof(Offset)) << "\" : \""
               << (valueImage + 6 * sizeof(Offset)) << "\"\n";
      }
      if (explain) {
      }
    }

    return true;
  }
};
}  // namespace Linux
}  // namespace chap
