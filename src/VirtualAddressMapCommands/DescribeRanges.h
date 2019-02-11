// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../Describer.h"
#include "../SizedTally.h"
#include "../VirtualMemoryPartition.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class DescribeRanges : public Commands::Subcommand {
 public:
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename VirtualMemoryPartition<Offset>::ClaimedRanges ClaimedRanges;
  DescribeRanges(const std::string& subcommandName,
                 const std::string& helpMessage,
                 const std::string& tallyDescriptor,
                 const ClaimedRanges& ranges,
                 const Describer<Offset>& describer, const char* keyForUnknown)
      : Commands::Subcommand("describe", subcommandName),
        _helpMessage(helpMessage),
        _tallyDescriptor(tallyDescriptor),
        _ranges(ranges),
        _describer(describer),
        _keyForUnknown(keyForUnknown) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << _helpMessage;
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, _tallyDescriptor);
    for (const auto& range : _ranges) {
      Offset rangeBase = range._base;
      tally.AdjustTally(range._size);
      output << "Range [0x" << std::hex << rangeBase << ", 0x" << range._limit
             << ") uses 0x" << range._size << " bytes.\n";
      output << "Region use: " << range._value << "\n";
      if (range._value != _keyForUnknown) {
        (void)_describer.Describe(context, rangeBase, false, false);
      }
      output << "\n";
    }
  }

 private:
  const std::string _helpMessage;
  const std::string _tallyDescriptor;
  const ClaimedRanges& _ranges;
  const Describer<Offset>& _describer;
  const char* _keyForUnknown;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
