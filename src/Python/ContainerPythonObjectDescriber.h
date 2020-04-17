// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace Python {
template <typename Offset>
class ContainerPythonObjectDescriber
    : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  ContainerPythonObjectDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage,
                                              "ContainerPythonObject"),
        _infrastructureFinder(processImage.GetPythonInfrastructureFinder()),
        _garbageCollectionHeaderSize(
            _infrastructureFinder.GarbageCollectionHeaderSize()),
        _contiguousImage(processImage.GetVirtualAddressMap(),
                         processImage.GetAllocationDirectory()) {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation,
                        bool /* explain */) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern ContainerPythonObject.\n";
    _contiguousImage.SetIndex(index);
    const char* firstChar = _contiguousImage.FirstChar();
    Offset referenceCount =
        *((Offset*)(firstChar + _garbageCollectionHeaderSize));
    Offset pythonType =
        *((Offset*)(firstChar + _garbageCollectionHeaderSize +
                    InfrastructureFinder<Offset>::TYPE_IN_PYOBJECT));
    output << "This has a PyGC_Head at the start so the real PyObject is at "
              "offset 0x"
           << std::hex << _garbageCollectionHeaderSize << ".\n";
    output << "This has reference count " << std::dec << referenceCount
           << " and python type 0x" << std::hex << pythonType;
    std::string pythonTypeName = _infrastructureFinder.GetTypeName(pythonType);
    if (!pythonTypeName.empty()) {
      output << " (" << pythonTypeName << ")";
    }
    output << "\n";
    std::string definedTypeName = _infrastructureFinder.GetTypeName(
        allocation.Address() + _garbageCollectionHeaderSize);
    if (!definedTypeName.empty()) {
      output << "This defines type \"" << definedTypeName << "\".\n";
    }
  }

 private:
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const Offset _garbageCollectionHeaderSize;
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
};
}  // namespace Python
}  // namespace chap
