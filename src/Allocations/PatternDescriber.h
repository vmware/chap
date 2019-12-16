// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ProcessImage.h"

namespace chap {
namespace Allocations {
template <typename Offset>
class PatternDescriber {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  PatternDescriber(const ProcessImage<Offset>& processImage,
                   const std::string& name)
      : _name(name),
        _processImage(processImage),
        _addressMap(processImage.GetVirtualAddressMap()),
        _finder(processImage.GetAllocationFinder()),
        _graph(processImage.GetAllocationGraph()),
        _moduleDirectory(processImage.GetModuleDirectory()),
        _tagHolder(processImage.GetAllocationTagHolder()) {}

  const std::string& GetName() const { return _name; }

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool explain) const = 0;

 protected:
  const std::string _name;
  const ProcessImage<Offset>& _processImage;
  const VirtualAddressMap<Offset>& _addressMap;
  const Finder<Offset>* _finder;
  const Graph<Offset>* _graph;
  const ModuleDirectory<Offset>& _moduleDirectory;
  const Allocations::TagHolder<Offset>* _tagHolder;
};
}  // namespace Allocations
}  // namespace chap
