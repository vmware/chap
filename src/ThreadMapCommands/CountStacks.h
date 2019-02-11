// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../SizedTally.h"
#include "../ThreadMap.h"
namespace chap {
namespace ThreadMapCommands {
template <class Offset>
class CountStacks : public Commands::Subcommand {
 public:
  CountStacks(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("count", "stacks"),
        _processImage(processImage),
        _threadMap(processImage.GetThreadMap()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command provides "
           "totals of the number of threads and the space they occupy.\n";
  }

  void Run(Commands::Context& context) {
    SizedTally<Offset> tally(context, "stacks");
    for (const auto& threadInfo : _threadMap) {
      tally.AdjustTally(threadInfo._stackLimit - threadInfo._stackBase);
    }
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const ThreadMap<Offset>& _threadMap;
};
}  // namespace ThreadMapCommands
}  // namespace chap
