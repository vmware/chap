// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ProcessImage.h"
#include "Finder.h"
#include "Graph.h"

namespace chap {
namespace Allocations {
template <typename Offset>
class PatternRecognizer {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  PatternRecognizer(const ProcessImage<Offset>& processImage,
                    const std::string& name)
      : _name(name),
        _processImage(processImage),
        _addressMap(processImage.GetVirtualAddressMap()),
        _finder(processImage.GetAllocationFinder()),
        _graph(processImage.GetAllocationGraph()),
        _moduleDirectory(processImage.GetModuleDirectory()) {}

  const std::string& GetName() const { return _name; }

  virtual bool Matches(AllocationIndex index, const Allocation& allocation,
                       bool isUnsigned) const = 0;

  /*
 * If the address is matches any of the registered patterns, provide a
 * description for the address as belonging to that pattern
 * optionally with an additional explanation of why the address matches
 * the description.  Return true only if the allocation matches the
 * pattern.
 */
  virtual bool Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool isUnsigned,
                        bool explain) const = 0;

 protected:
  const std::string _name;
  const ProcessImage<Offset>& _processImage;
  const VirtualAddressMap<Offset>& _addressMap;
  const Finder<Offset>* _finder;
  const Graph<Offset>* _graph;
  const ModuleDirectory<Offset>& _moduleDirectory;
};
}  // namespace Allocations
}  // namespace chap
