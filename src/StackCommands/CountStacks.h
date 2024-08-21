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
class CountStacks : public Commands::Subcommand {
 public:
  CountStacks(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("count", "stacks"),
        _processImage(processImage),
        _stackRegistry(processImage.GetStackRegistry()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command provides "
           "totals of the number of stacks and the space they occupy.\n";
  }

  void Run(Commands::Context& context) {
    SizedTally<Offset> tally(context, "stacks");
    _stackRegistry.VisitStacks([&tally](Offset regionBase, Offset regionLimit,
                                        const char*, Offset, Offset, size_t) {
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
