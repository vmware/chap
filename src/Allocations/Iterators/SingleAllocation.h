// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Directory.h"
#include "../SetCache.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class SingleAllocation {
 public:
  class Factory {
   public:
    Factory() : _setName("allocation") {}
    SingleAllocation* MakeIterator(Commands::Context& context,
                                   const ProcessImage<Offset>&,
                                   const Directory<Offset>& directory,
                                   const SetCache<Offset>&) {
      SingleAllocation* iterator = 0;
      AllocationIndex numAllocations = directory.NumAllocations();
      size_t numPositionals = context.GetNumPositionals();
      Commands::Error& error = context.GetError();
      if (numPositionals < 3) {
        error << "No address was specified for a single allocation.\n";
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
            iterator = new SingleAllocation(index, numAllocations);
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
      output << "Use \"allocation <address-in-hex>\" to specify"
                " a set with just the allocation\n"
                "containing the specified address.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;

  SingleAllocation(AllocationIndex index, AllocationIndex numAllocations)
      : _index(index), _numAllocations(numAllocations), _visitedFirst(false) {}
  AllocationIndex Next() {
    if (_visitedFirst) {
      return _numAllocations;
    } else {
      _visitedFirst = true;
      return _index;
    }
  }

 private:
  AllocationIndex _index;
  AllocationIndex _numAllocations;
  bool _visitedFirst;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
