// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace Python {
template <typename Offset>
class PyDictKeysObjectDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  PyDictKeysObjectDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "PyDictKeysObject"),
        _stringTypeObj(0),
        _contiguousImage(*(processImage.GetAllocationFinder())) {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& /* allocation */,
                        bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern PyDictKeysObject.\n";
    _contiguousImage.SetIndex(index);
    const Offset* asOffsets = _contiguousImage.FirstOffset();
    Offset numSlots = asOffsets[1];
    for (Offset slot = 0; slot < numSlots; slot++) {
      Offset key = ((Offset*)(asOffsets))[5 + slot * 3];
      Offset value = ((Offset*)(asOffsets))[6 + slot * 3];
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
        if (strlen(keyImage + 6 * sizeof(Offset)) != ((Offset*)(keyImage))[2]) {
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

 private:
  mutable Offset _stringTypeObj;
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
};
}  // namespace Python
}  // namespace chap
