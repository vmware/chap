// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../../SizedTally.h"
#include "../Describer.h"
#include "../Finder.h"
namespace chap {
namespace Allocations {
namespace Visitors {
template <class Offset>
class Explainer {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  class Factory {
   public:
    Factory()
        : _inModuleDescriber(0),
          _stackDescriber(0),
          _describer(_inModuleDescriber, _stackDescriber, 0),
          _commandName("explain") {}
    Explainer* MakeVisitor(Commands::Context& context,
                           const ProcessImage<Offset>& processImage) {
      _describer.SetProcessImage(&processImage);
      return new Explainer(context, _describer);
    }
    const std::string& GetCommandName() const { return _commandName; }
    // TODO: allow adding taints
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "In this case \"explain\" means show the address, size, "
                "anchored/leaked/free\n"
                "status and type if known, with the reason that the given "
                "status applies.\n";
    }

   private:
    InModuleDescriber<Offset> _inModuleDescriber;
    StackDescriber<Offset> _stackDescriber;
    Allocations::Describer<Offset> _describer;
    const std::string _commandName;
    const std::vector<std::string> _taints;
  };

  Explainer(Commands::Context& context,
            const Allocations::Describer<Offset>& describer)
      : _context(context),
        _describer(describer),
        _sizedTally(context, "allocations") {}
  void Visit(AllocationIndex index, const Allocation& allocation) {
    size_t size = allocation.Size();
    _sizedTally.AdjustTally(size);
    _describer.Describe(_context, index, allocation, true);
  }

 private:
  Commands::Context& _context;
  const Allocations::Describer<Offset>& _describer;
  SizedTally<Offset> _sizedTally;
};
}  // namespace Visitors
}  // namespace Allocations
}  // namespace chap
