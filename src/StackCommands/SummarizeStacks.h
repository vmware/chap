// Copyright (c) 2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../SizedTally.h"
#include "../StackRegistry.h"
namespace chap {
namespace StackCommands {
template <class Offset>
class SummarizeStacks : public Commands::Subcommand {
 public:
  SummarizeStacks(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("summarize", "stacks"),
        _processImage(processImage),
        _stackRegistry(processImage.GetStackRegistry()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This provides "
           "totals of the number of stacks of each type and the "
           "space they occupy.\n";
  }

  void Run(Commands::Context& context) {
    SizedTally<Offset> tally(context, "stacks");
    std::map<const char*, CountAndTotal> talliesByType;
    _stackRegistry.VisitStacks([&tally, &talliesByType](
        Offset regionBase, Offset regionLimit, const char* stackType, Offset,
        Offset, size_t) {

      tally.AdjustTally(regionLimit - regionBase);
      CountAndTotal& countAndTotal = talliesByType[stackType];
      countAndTotal._count++;
      countAndTotal._total += (regionLimit - regionBase);
      return true;  // continue traversal
    });
    Commands::Output& output = context.GetOutput();
    for (const auto& typeWithTally : talliesByType) {
      output << std::dec << typeWithTally.second._count << " "
             << typeWithTally.first << "s use 0x" << std::hex
             << typeWithTally.second._total << " bytes.\n";
    }
  }

 private:
  struct CountAndTotal {
    CountAndTotal() : _count(0), _total(0) {}
    Offset _count;
    Offset _total;
  };
  const ProcessImage<Offset>& _processImage;
  const StackRegistry<Offset>& _stackRegistry;
};
}  // namespace StackCommands
}  // namespace chap
