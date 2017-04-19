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
class Used {
 public:
  class Factory {
   public:
    Factory() : _setName("used") {}
    Used* MakeIterator(Commands::Context& context,
                       const ProcessImage<Offset>& processImage,
                       const Finder<Offset>& allocationFinder) {
      return new Used(allocationFinder, allocationFinder.NumAllocations());
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 0; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "Use \"used\" to specify"
                " the set of all used allocations.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;

  Used(const Finder<Offset>& allocationFinder, AllocationIndex numAllocations)
      : _index(0),
        _allocationFinder(allocationFinder),
        _numAllocations(numAllocations) {}
  AllocationIndex Next() {
    while (_index != _numAllocations &&
           !_allocationFinder.AllocationAt(_index)->IsUsed()) {
      ++_index;
    }
    AllocationIndex next = _index;
    if (_index != _numAllocations) {
      ++_index;
    }
    return next;
  }

 private:
  AllocationIndex _index;
  const Finder<Offset>& _allocationFinder;
  AllocationIndex _numAllocations;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
