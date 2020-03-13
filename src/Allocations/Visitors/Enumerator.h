// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Directory.h"
namespace chap {
namespace Allocations {
namespace Visitors {
template <class Offset>
class Enumerator {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  class Factory {
   public:
    Factory() : _commandName("enumerate") {}
    Enumerator* MakeVisitor(Commands::Context& context,
                            const ProcessImage<Offset>& /* processImage */) {
      return new Enumerator(context);
    }
    const std::string& GetCommandName() const { return _commandName; }
    // TODO: allow adding taints
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "In this case \"enumerate\" means show the address of "
                "each allocation in the set.\n";
    }

   private:
    const std::string _commandName;
    const std::vector<std::string> _taints;
  };

  Enumerator(Commands::Context& context) : _context(context) {}
  void Visit(AllocationIndex /* index */, const Allocation& allocation) {
    _context.GetOutput() << std::hex << allocation.Address() << "\n";
  }

 private:
  Commands::Context& _context;
};
}  // namespace Visitors
}  // namespace Allocations
}  // namespace chap
