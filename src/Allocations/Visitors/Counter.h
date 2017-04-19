// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../../SizedTally.h"
#include "../Finder.h"
namespace chap {
namespace Allocations {
namespace Visitors {
template <class Offset>
class Counter {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  class Factory {
   public:
    Factory() : _commandName("count") {}
    Counter* MakeVisitor(Commands::Context& context,
                         const ProcessImage<Offset>& processImage,
                         const Finder<Offset>& allocationFinder) {
      return new Counter(context);
    }
    const std::string& GetCommandName() const { return _commandName; }
    // TODO: allow adding taints
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "In this case \"count\" means show the number of "
             << "allocations in the set and the\n"
             << "total bytes used by those allocations.\n";
    }

   private:
    const std::string _commandName;
    const std::vector<std::string> _taints;
  };

  Counter(Commands::Context& context)
      : _context(context), _sizedTally(context, "allocations") {}
  void Visit(AllocationIndex index, const Allocation& allocation) {
    _sizedTally.AdjustTally(allocation.Size());
  }

 private:
  Commands::Context& _context;
  SizedTally<Offset> _sizedTally;
};
}  // namespace Visitors
}  // namespace Allocations
}  // namespace chap
