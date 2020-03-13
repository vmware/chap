// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Directory.h"
#include "../Graph.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class ExternalAnchored {
 public:
  class Factory {
   public:
    Factory() : _setName("externalanchored") {}
    ExternalAnchored* MakeIterator(Commands::Context& /* context */,
                                   const ProcessImage<Offset>& processImage,
                                   const Directory<Offset>& directory) {
      const Graph<Offset>* allocationGraph = processImage.GetAllocationGraph();
      if (allocationGraph == 0) {
        return (ExternalAnchored*)(0);
      }
      return new ExternalAnchored(directory, directory.NumAllocations(),
                                  *allocationGraph);
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 0; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output
          << "Use \"externalanchored\" to specify"
             " the set of all allocations anchored\n"
             "externally from outside the process.  This anchoring is guessed"
             " based on some\n"
             "pattern in the allocation rather than by some"
             " incoming edge in the process\n"
             "image.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;

  ExternalAnchored(const Directory<Offset>& directory,
                   AllocationIndex numAllocations,
                   const Graph<Offset>& allocationGraph)
      : _index(0),
        _directory(directory),
        _numAllocations(numAllocations),
        _allocationGraph(allocationGraph) {}
  AllocationIndex Next() {
    while (_index != _numAllocations &&
           !_allocationGraph.IsExternalAnchored(_index)) {
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
  const Directory<Offset>& _directory;
  AllocationIndex _numAllocations;
  const Graph<Offset>& _allocationGraph;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
