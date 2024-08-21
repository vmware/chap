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
class DescribeStacks : public Commands::Subcommand {
 public:
  DescribeStacks(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("describe", "stacks"),
        _processImage(processImage),
        _stackRegistry(processImage.GetStackRegistry()),
        _addressMap(processImage.GetVirtualAddressMap()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command describes the stacks and provides "
           "totals of the\nnumber of stacks and the space they occupy.\n";
  }

  void Run(Commands::Context& context) {
    SizedTally<Offset> tally(context, "stacks");
    typename VirtualAddressMap<Offset>::Reader reader(_addressMap);
    Commands::Output& output = context.GetOutput();
    _stackRegistry.VisitStacks([&tally, &output, &reader](
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

      output << ".\n";
      Offset check0 = regionBase;
      while (check0 < regionLimit && reader.ReadOffset(check0, 0xbad) == 0) {
        check0 += sizeof(Offset);
      }
      Offset totalRangeBytes = regionLimit - regionBase;
      Offset peakStackUsage = regionLimit - check0;
      if (peakStackUsage + 0x1000 < totalRangeBytes) {
        output << "Peak stack usage was 0x" << std::hex << peakStackUsage
               << " bytes out of 0x" << totalRangeBytes << " total.\n";
      }
      output << "\n";

      tally.AdjustTally(regionLimit - regionBase);
      return true;  // continue traversal
    });
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const StackRegistry<Offset>& _stackRegistry;
  const VirtualAddressMap<Offset>& _addressMap;
};
}  // namespace StackCommands
}  // namespace chap
