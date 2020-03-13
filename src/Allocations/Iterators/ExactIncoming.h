// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../ContiguousImage.h"
#include "../Directory.h"
#include "../Graph.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class ExactIncoming {
 public:
  class Factory {
   public:
    Factory() : _setName("exactincoming") {}
    ExactIncoming* MakeIterator(Commands::Context& context,
                                const ProcessImage<Offset>& processImage,
                                const Directory<Offset>& directory) {
      ExactIncoming* iterator = 0;
      AllocationIndex numAllocations = directory.NumAllocations();
      size_t numPositionals = context.GetNumPositionals();
      Commands::Error& error = context.GetError();
      if (numPositionals < 3) {
        error << "No address was specified for the target allocation.\n";
      } else {
        Offset address;
        if (!context.ParsePositional(2, address)) {
          error << context.Positional(2) << " is not a valid address.\n";
        } else {
          AllocationIndex index = directory.AllocationIndexOf(address);
          if (index == numAllocations) {
            error << context.Positional(2)
                  << " is not part of an allocation.\n";
          } else {
            const Graph<Offset>* allocationGraph =
                processImage.GetAllocationGraph();
            if (allocationGraph != 0) {
              iterator = new ExactIncoming(directory, *allocationGraph,
                                           processImage.GetVirtualAddressMap(),
                                           index, numAllocations);
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
      output
          << "Use \"exactincoming <address-in-hex>\""
             " to specify the set of all allocations that\n"
             "reference the start of the allocation that contains the specified"
             " address.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;

  ExactIncoming(const Directory<Offset>& directory, const Graph<Offset>& graph,
                const VirtualAddressMap<Offset>& addressMap,
                AllocationIndex index, AllocationIndex numAllocations)
      : _directory(directory),
        _contiguousImage(addressMap, _directory),
        _graph(graph),
        _addressMap(addressMap),
        _index(index),
        _numAllocations(numAllocations) {
    _target = _directory.AllocationAt(index)->Address();
    _graph.GetIncoming(index, &_pNextIncoming, &_pPastIncoming);
  }
  AllocationIndex Next() {
    while (_pNextIncoming != _pPastIncoming) {
      AllocationIndex index = *(_pNextIncoming++);
      const Allocation* allocation = _directory.AllocationAt(index);
      if (allocation == ((Allocation*)(0))) {
        abort();
      }
      if (allocation->IsUsed()) {
        _contiguousImage.SetIndex(index);
        const Offset* firstOffset = _contiguousImage.FirstOffset();
        const Offset* offsetLimit = _contiguousImage.OffsetLimit();
        for (const Offset* nextOffset = firstOffset; nextOffset != offsetLimit;
             nextOffset++) {
          if (_target == *nextOffset) {
            return index;
          }
        }
      }
    }
    return _numAllocations;
  }

 private:
  const Directory<Offset>& _directory;
  ContiguousImage<Offset> _contiguousImage;
  const Graph<Offset>& _graph;
  const VirtualAddressMap<Offset>& _addressMap;
  AllocationIndex _index;
  AllocationIndex _numAllocations;
  const AllocationIndex* _pNextIncoming;
  const AllocationIndex* _pPastIncoming;
  Offset _target;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
