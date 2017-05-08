// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Finder.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class Allocations {
 public:
  class Factory {
   public:
    Factory() : _setName("allocations") {}
    Allocations* MakeIterator(Commands::Context& /* context */,
                              const ProcessImage<Offset>& /* processImage */,
                              const Finder<Offset>& allocationFinder) {
      return new Allocations(allocationFinder.NumAllocations());
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 0; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "Use \"allocations\" to specify"
                " the set of all allocations, both used and free.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;

  Allocations(AllocationIndex numAllocations)
      : _index(0), _numAllocations(numAllocations) {}
  AllocationIndex Next() {
    AllocationIndex next = _index;
    if (_index != _numAllocations) {
      ++_index;
    }
    return next;
  }

 private:
  AllocationIndex _index;
  AllocationIndex _numAllocations;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
