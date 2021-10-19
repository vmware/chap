// Copyright (c) 2017,2020-2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Directory.h"
#include "../EdgePredicate.h"
#include "../Graph.h"
#include "../SetCache.h"
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
                               const Directory<Offset>& directory,
                               const SetCache<Offset>&) {
      AllocationIndex numAllocations = directory.NumAllocations();
      Commands::Error& error = context.GetError();

      size_t numPositionals = context.GetNumPositionals();
      if (numPositionals < 3) {
        error << "No address was specified for the target allocation.\n";
        return nullptr;
      }

      Offset address;
      if (!context.ParsePositional(2, address)) {
        error << context.Positional(2) << " is not a valid address.\n";
        return nullptr;
      }

      AllocationIndex index = directory.AllocationIndexOf(address);
      if (index == numAllocations) {
        error << context.Positional(2) << " is not part of an allocation.\n";
        return nullptr;
      }

      const Graph<Offset>* allocationGraph = processImage.GetAllocationGraph();
      if (allocationGraph == 0) {
        error << "Allocation graph is not available.\n";
        return nullptr;
      }

      bool skipTaintedReferences = false;
      if (!context.ParseBooleanSwitch("skipTaintedReferences",
                                      skipTaintedReferences)) {
        return nullptr;
      }

      bool skipUnfavoredReferences = false;
      if (!context.ParseBooleanSwitch("skipUnfavoredReferences",
                                      skipUnfavoredReferences)) {
        return nullptr;
      }

      /*
       * Allow /skipUnfavoredReferences as a flag, but ignore it
       * because there is no such thing as favored references or
       * unfavored references for free allocations.
       */

      return new FreeOutgoing(directory, *allocationGraph, index,
                              numAllocations, processImage.GetEdgeIsTainted(),
                              skipTaintedReferences);
    }

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
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  typedef typename Graph<Offset>::EdgeIndex EdgeIndex;
  FreeOutgoing(const Directory<Offset>& directory, const Graph<Offset>& graph,
               AllocationIndex index, AllocationIndex numAllocations,
               const EdgePredicate<Offset>* edgeIsTainted,
               bool skipTaintedReferences)
      : _directory(directory),
        _graph(graph),
        _index(index),
        _numAllocations(numAllocations),
        _edgeIsTainted(*edgeIsTainted),
        _skipTaintedReferences(skipTaintedReferences) {
    _graph.GetOutgoing(index, _nextOutgoing, _pastOutgoing);
  }

  AllocationIndex Next() {
    for (; _nextOutgoing != _pastOutgoing; _nextOutgoing++) {
      if (_skipTaintedReferences && _edgeIsTainted.ForOutgoing(_nextOutgoing)) {
        continue;
      }
      /*
       * The /skipUnfavoredReferences switch is irrelevant, because a free
       * allocation isn't tagged, so an edge for which the target is a free
       * allocation is neither favored nor unfavored.
       */
      AllocationIndex index = _graph.GetTargetForOutgoing(_nextOutgoing);
      const Allocation* allocation = _directory.AllocationAt(index);
      if (allocation == nullptr) {
        abort();
      }
      if (!allocation->IsUsed()) {
        _nextOutgoing++;
        return index;
      }
    }
    return _numAllocations;
  }

 private:
  const Directory<Offset>& _directory;
  const Graph<Offset>& _graph;
  AllocationIndex _index;
  AllocationIndex _numAllocations;
  const EdgePredicate<Offset>& _edgeIsTainted;
  bool _skipTaintedReferences;
  EdgeIndex _nextOutgoing;
  EdgeIndex _pastOutgoing;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
