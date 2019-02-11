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
class ListStacks : public Commands::Subcommand {
 public:
  ListStacks(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("list", "stacks"),
        _processImage(processImage),
        _threadMap(processImage.GetThreadMap()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command lists the stacks for every thread and provides "
           "totals of the\nnumber of threads and the space they occupy.\n";
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, "stacks");
    for (const auto& threadInfo : _threadMap) {
      output << "Thread " << std::dec << threadInfo._threadNum
             << " uses stack block [0x" << std::hex << threadInfo._stackBase
             << ", " << threadInfo._stackLimit << ") current sp: 0x"
             << threadInfo._stackPointer << "\n";
      tally.AdjustTally(threadInfo._stackLimit - threadInfo._stackBase);
    }
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const ThreadMap<Offset>& _threadMap;
};
}  // namespace ThreadMapCommands
}  // namespace chap
