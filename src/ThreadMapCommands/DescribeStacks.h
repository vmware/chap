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
class DescribeStacks : public Commands::Subcommand {
 public:
  DescribeStacks(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("describe", "stacks"),
        _processImage(processImage),
        _threadMap(processImage.GetThreadMap()),
        _addressMap(processImage.GetVirtualAddressMap()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command describes the stacks for every thread and provides "
           "totals of the\nnumber of threads and the space they occupy.\n";
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, "stacks");
    typename VirtualAddressMap<Offset>::Reader reader(_addressMap);
    for (const auto& threadInfo : _threadMap) {
      Offset base = threadInfo._stackBase;
      Offset limit = threadInfo._stackLimit;
      Offset check0 = base;
      while (check0 < limit && reader.ReadOffset(check0, 0xbad) == 0) {
        check0 += sizeof(Offset);
      }
      Offset peakStackUsage = limit - check0;
      output << "Thread " << std::dec << threadInfo._threadNum
             << " uses stack block [0x" << std::hex << base << ", " << limit
             << ")\n current sp: 0x" << threadInfo._stackPointer << "\n";
      output << "Peak stack usage was 0x" << peakStackUsage
             << " bytes out of 0x" << (limit - base) << " total.\n\n";
      tally.AdjustTally(limit - base);
    }
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const ThreadMap<Offset>& _threadMap;
  const VirtualAddressMap<Offset>& _addressMap;
};
}  // namespace ThreadMapCommands
}  // namespace chap
