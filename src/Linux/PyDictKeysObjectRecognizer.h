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
  typedef typename Allocations::TagHolder<Offset>::TagIndex TagIndex;
  PyDictKeysObjectRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage,
                                               "PyDictKeysObject"),
        _stringTypeObj(0),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _contiguousImage(*(processImage.GetAllocationFinder())),
        _tagIndex(~((TagIndex)(0))) {
    const PythonAllocationsTagger<Offset>* tagger =
        processImage.GetPythonAllocationsTagger();
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
      return true;
    }
    return false;
  }

 private:
  mutable Offset _stringTypeObj;
  const Allocations::TagHolder<Offset>* _tagHolder;
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
  TagIndex _tagIndex;
};
}  // namespace Linux
}  // namespace chap
