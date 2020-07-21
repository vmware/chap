// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace Python {
template <typename Offset>
class SimplePythonObjectDescriber
    : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  SimplePythonObjectDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage,
                                              "SimplePythonObject"),
        _infrastructureFinder(processImage.GetPythonInfrastructureFinder()),
        _strType(_infrastructureFinder.StrType()),
        _cstringInStr(_infrastructureFinder.CstringInStr()),
        _contiguousImage(processImage.GetVirtualAddressMap(),
                         processImage.GetAllocationDirectory()) {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& /* allocation */,
                        bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern SimplePythonObject.\n";
    _contiguousImage.SetIndex(index);
    const Offset* asOffsets = _contiguousImage.FirstOffset();
    Offset referenceCount = asOffsets[0];
    Offset pythonType = asOffsets[1];
    output << "This has reference count " << std::dec << referenceCount
           << " and python type 0x" << std::hex << pythonType;
    std::string pythonTypeName = _infrastructureFinder.GetTypeName(pythonType);
    if (!pythonTypeName.empty()) {
      output << " (" << pythonTypeName << ")";
    }
    output << "\n";
    if (pythonType == _strType) {
      Offset stringLength = asOffsets[2];
      output << "This has a string of length " << std::dec << stringLength;
      const char* stringStart = _contiguousImage.FirstChar() + _cstringInStr;
      if (explain || stringLength < 77) {
        output << " containing\n\"";
        output.ShowEscapedAscii(stringStart, stringLength);
        output << "\".\n";
      } else {
        output << " starting with\n\"";
        output.ShowEscapedAscii(stringStart, 77);
        output << "\".\n";
      }
    }
  }

 private:
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const Offset _strType;
  const Offset _cstringInStr;
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
};
}  // namespace Python
}  // namespace chap
