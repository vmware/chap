// Copyright (c) 2017,2020-2021 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Directory.h"
#include "../EdgePredicate.h"
#include "../Graph.h"
#include "../SetCache.h"
#include "../TagHolder.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class Outgoing {
 public:
  class Factory {
   public:
    Factory() : _setName("outgoing") {}
    Outgoing* MakeIterator(Commands::Context& context,
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

      return new Outgoing(
          directory, *allocationGraph, index, numAllocations,
          processImage.GetAllocationTagHolder(),
          processImage.GetEdgeIsTainted(), skipTaintedReferences,
          processImage.GetEdgeIsFavored(), skipUnfavoredReferences);
    }

    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 1; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "Use \"outgoing <address-in-hex>\""
                " to specify the set of all used allocations\n"
                "that are referenced by the allocation that contains the"
                " specified address.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  typedef typename Graph<Offset>::EdgeIndex EdgeIndex;

  Outgoing(const Directory<Offset>& directory, const Graph<Offset>& graph,
           AllocationIndex index, AllocationIndex numAllocations,
           const TagHolder<Offset>* tagHolder,
           const EdgePredicate<Offset>* edgeIsTainted,
           bool skipTaintedReferences,
           const EdgePredicate<Offset>* edgeIsFavored,
           bool skipUnfavoredReferences)
      : _directory(directory),
        _graph(graph),
        _index(index),
        _numAllocations(numAllocations),
        _tagHolder(*tagHolder),
        _edgeIsTainted(*edgeIsTainted),
        _skipTaintedReferences(skipTaintedReferences),
        _edgeIsFavored(*edgeIsFavored),
        _skipUnfavoredReferences(skipUnfavoredReferences) {
    _graph.GetOutgoing(index, _nextOutgoing, _pastOutgoing);
  }

  AllocationIndex Next() {
    for (; _nextOutgoing != _pastOutgoing; _nextOutgoing++) {
      if (_skipTaintedReferences && _edgeIsTainted.ForOutgoing(_nextOutgoing)) {
        continue;
      }
      AllocationIndex index = _graph.GetTargetForOutgoing(_nextOutgoing);
      if (_skipUnfavoredReferences &&
          _tagHolder.SupportsFavoredReferences(index) &&
          !_edgeIsFavored.ForOutgoing(_nextOutgoing)) {
        continue;
      }
      const Allocation* allocation = _directory.AllocationAt(index);
      if (allocation == nullptr) {
        abort();
      }
      if (allocation->IsUsed()) {
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
  const TagHolder<Offset>& _tagHolder;
  const EdgePredicate<Offset>& _edgeIsTainted;
  bool _skipTaintedReferences;
  const EdgePredicate<Offset>& _edgeIsFavored;
  bool _skipUnfavoredReferences;
  EdgeIndex _nextOutgoing;
  EdgeIndex _pastOutgoing;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
