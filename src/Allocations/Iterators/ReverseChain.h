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
class ReverseChain {
 public:
  class Factory {
   public:
    Factory() : _setName("reversechain") {}
    ReverseChain* MakeIterator(Commands::Context& context,
                               const ProcessImage<Offset>& processImage,
                               const Finder<Offset>& allocationFinder) {
      ReverseChain* iterator = 0;
      AllocationIndex numAllocations = allocationFinder.NumAllocations();
      size_t numPositionals = context.GetNumPositionals();
      Commands::Error& error = context.GetError();
      if (numPositionals < 5) {
        if (numPositionals < 4) {
          if (numPositionals < 3) {
            error << "No address was specified for a single allocation.\n";
          }
          error << "No offset was provided for the edge source.\n";
        }
        error << "No offset was specified for the edge target.\n";
      } else {
        Offset address;
        Offset linkOffset;
        Offset targetOffset;
        if (!context.ParsePositional(2, address)) {
          error << context.Positional(2) << " is not a valid address.\n";
        } else if (!context.ParsePositional(3, linkOffset)) {
          error << context.Positional(3)
                << " is not a valid offset in the edge source.\n";
        } else if (!context.ParsePositional(4, targetOffset)) {
          error << context.Positional(4)
                << " is not a valid offset for the edge target.\n";
        } else {
          AllocationIndex index = allocationFinder.AllocationIndexOf(address);
          if (index == numAllocations) {
            error << context.Positional(2)
                  << " is not part of an allocation.\n";
          } else {
            const Graph<Offset>* allocationGraph =
                processImage.GetAllocationGraph();
            if (allocationGraph != 0) {
              iterator =
                  new ReverseChain(allocationFinder, *allocationGraph,
                                   processImage.GetVirtualAddressMap(), index,
                                   numAllocations, linkOffset, targetOffset);
            }
          }
        }
      }
      return iterator;
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 3; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output
          << "Use \"reversechain <address-in-hex>"
             " <source-offset> <target-offset>\" to\n"
             "specify a set starting at the allocation containing the"
             " specified address and\n"
             "following incoming edges that are constrained so that the"
             " reference is at the\n"
             "specified offset in the source and points"
             " to the specified offset in the\n"
             "target. This is intended for following long singly linked lists"
             " backwards.  The\n"
             "chain is terminated either when no suitable"
             " incoming edge exists or when\nmultiple such edges do.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;

  ReverseChain(const Finder<Offset>& finder, const Graph<Offset>& graph,
               const VirtualAddressMap<Offset>& addressMap,
               AllocationIndex index, AllocationIndex numAllocations,
               Offset linkOffset, Offset targetOffset)
      : _finder(finder),
        _graph(graph),
        _addressMap(addressMap),
        _index(index),
        _numAllocations(numAllocations),
        _linkOffset(linkOffset),
        _targetOffset(targetOffset) {}
  AllocationIndex Next() {
    AllocationIndex returnValue = _index;
    if (_index != _numAllocations) {
      const Allocation* target = _finder.AllocationAt(_index);
      if (target == 0) {
        abort();
      }
      if (target->Size() >= _targetOffset) {
        Offset targetAddress = target->Address();
        const AllocationIndex* pNextIncoming;
        const AllocationIndex* pPastIncoming;
        _graph.GetIncoming(_index, &pNextIncoming, &pPastIncoming);

        _index = _numAllocations;
        bool suitableIncomingFound = false;
        typename VirtualAddressMap<Offset>::Reader reader(_addressMap);
        while (pNextIncoming != pPastIncoming) {
          AllocationIndex index = *(pNextIncoming++);
          const Allocation* source = _finder.AllocationAt(index);
          if (source == ((Allocation*)(0))) {
            abort();
          }
          if ((source->Size() >= _linkOffset + sizeof(Offset)) &&
              (reader.ReadOffset(source->Address() + _linkOffset) ==
               (targetAddress + _targetOffset))) {
            if (!suitableIncomingFound) {
              suitableIncomingFound = true;
              _index = index;
            } else {
              _index = _numAllocations;
              break;
            }
          }
        }
      } else {
        _index = _numAllocations;
      }
    }
    return returnValue;
  }

 private:
  const Finder<Offset>& _finder;
  const Graph<Offset>& _graph;
  const VirtualAddressMap<Offset>& _addressMap;
  AllocationIndex _index;
  AllocationIndex _numAllocations;
  Offset _linkOffset;
  Offset _targetOffset;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
