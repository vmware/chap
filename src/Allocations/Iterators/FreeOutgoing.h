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
class FreeOutgoing {
 public:
  class Factory {
   public:
    Factory() : _setName("freeoutgoing") {}
    FreeOutgoing* MakeIterator(Commands::Context& context,
                           const ProcessImage<Offset>& processImage,
                           const Finder<Offset>& allocationFinder) {
      FreeOutgoing* iterator = 0;
      AllocationIndex numAllocations = allocationFinder.NumAllocations();
      size_t numPositionals = context.GetNumPositionals();
      Commands::Error& error = context.GetError();
      if (numPositionals < 3) {
        error << "No address was specified for the target allocation.\n";
      } else {
        Offset address;
        if (!context.ParsePositional(2, address)) {
          error << context.Positional(2) << " is not a valid address.\n";
        } else {
          AllocationIndex index = allocationFinder.AllocationIndexOf(address);
          if (index == numAllocations) {
            error << context.Positional(2)
                  << " is not part of an allocation.\n";
          } else {
            const Graph<Offset>* allocationGraph =
                processImage.GetAllocationGraph();
            if (allocationGraph != 0) {
              iterator = new FreeOutgoing(allocationFinder, *allocationGraph, index,
                                      numAllocations);
            }
          }
        }
      }
      return iterator;
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 1; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "Use \"freeoutgoing <address-in-hex>\""
                " to specify the set of all free allocations\n"
                "that are referenced by the allocation that contains the"
                " specified address.\n"
                "At present many of these references are likely to be false."
                "\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;

  FreeOutgoing(const Finder<Offset>& finder, const Graph<Offset>& graph,
           AllocationIndex index, AllocationIndex numAllocations)
      : _finder(finder),
        _graph(graph),
        _index(index),
        _numAllocations(numAllocations) {
    _graph.GetOutgoing(index, &_pNextOutgoing, &_pPastOutgoing);
  }

  AllocationIndex Next() {
    while (_pNextOutgoing != _pPastOutgoing) {
      AllocationIndex index = *(_pNextOutgoing++);
      const Allocation* allocation = _finder.AllocationAt(index);
      if (allocation == ((Allocation*)(0))) {
        abort();
      }
      if (!allocation->IsUsed()) {
        return index;
      }
    }
    return _numAllocations;
  }

 private:
  const Finder<Offset>& _finder;
  const Graph<Offset>& _graph;
  AllocationIndex _index;
  AllocationIndex _numAllocations;
  const AllocationIndex* _pNextOutgoing;
  const AllocationIndex* _pPastOutgoing;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
