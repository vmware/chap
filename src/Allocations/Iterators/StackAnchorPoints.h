// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Finder.h"
#include "../Graph.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class StackAnchorPoints {
 public:
  class Factory {
   public:
    Factory() : _setName("stackanchorpoints") {}
    StackAnchorPoints* MakeIterator(Commands::Context& /* context */,
                                    const ProcessImage<Offset>& processImage,
                                    const Finder<Offset>& allocationFinder) {
      const Graph<Offset>* allocationGraph = processImage.GetAllocationGraph();
      if (allocationGraph == 0) {
        return (StackAnchorPoints*)(0);
      }
      return new StackAnchorPoints(allocationFinder,
                                   allocationFinder.NumAllocations(),
                                   *allocationGraph);
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 0; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "Use \"stackanchorpoints\" to specify"
                " the set of all allocations directly\n"
                "referenced by at least one thread's stack.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;

  StackAnchorPoints(const Finder<Offset>& allocationFinder,
                    AllocationIndex numAllocations,
                    const Graph<Offset>& allocationGraph)
      : _index(0),
        _allocationFinder(allocationFinder),
        _numAllocations(numAllocations),
        _allocationGraph(allocationGraph) {}
  AllocationIndex Next() {
    while (_index != _numAllocations &&
           !_allocationGraph.IsStackAnchorPoint(_index)) {
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
  const Graph<Offset>& _allocationGraph;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
