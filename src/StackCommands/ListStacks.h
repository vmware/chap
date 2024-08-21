// Copyright (c) 2021 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../SizedTally.h"
#include "../StackRegistry.h"
namespace chap {
namespace StackCommands {
template <class Offset>
class ListStacks : public Commands::Subcommand {
 public:
  ListStacks(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("list", "stacks"),
        _processImage(processImage),
        _stackRegistry(processImage.GetStackRegistry()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command lists the stacks and provides "
           "totals of the\nnumber of stacks and the space they occupy.\n";
  }

  void Run(Commands::Context& context) {
    SizedTally<Offset> tally(context, "stacks");
    Commands::Output& output = context.GetOutput();
    _stackRegistry.VisitStacks([&tally, &output](
        Offset regionBase, Offset regionLimit, const char* stackType,
        Offset stackTop, Offset, size_t threadNum) {
      output << "Stack region [0x" << std::hex << regionBase << ", 0x"
             << regionLimit << ") is for a " << stackType;
      if (stackTop != StackRegistry<Offset>::STACK_TOP_UNKNOWN) {
        output << "\n with stack top 0x" << std::hex << stackTop;
      }
      if (threadNum != StackRegistry<Offset>::THREAD_NUMBER_UNKNOWN) {
        output << " used by thread " << std::dec << threadNum;
      }

      output << ".\n\n";

      tally.AdjustTally(regionLimit - regionBase);
      return true;  // continue traversal
    });
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const StackRegistry<Offset>& _stackRegistry;
};
}  // namespace StackCommands
}  // namespace chap
