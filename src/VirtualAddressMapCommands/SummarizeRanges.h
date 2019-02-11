// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <algorithm>
#include <map>
#include <vector>
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../SizedTally.h"
#include "../VirtualMemoryPartition.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class SummarizeRanges : public Commands::Subcommand {
 public:
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename VirtualMemoryPartition<Offset>::ClaimedRanges ClaimedRanges;
  SummarizeRanges(const std::string& subcommandName,
                  const std::string& helpMessage,
                  const std::string& tallyDescriptor,
                  const ClaimedRanges& ranges)
      : Commands::Subcommand("summarize", subcommandName),
        _helpMessage(helpMessage),
        _tallyDescriptor(tallyDescriptor),
        _ranges(ranges) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << _helpMessage;
  }

 private:
  typedef std::map<const char*, std::pair<Offset, Offset> > UseTallies;
  typedef typename UseTallies::value_type UseTalliesValue;
  struct Compare {
    bool operator()(const UseTalliesValue& left, const UseTalliesValue& right) {
      // Sort in decreasing order of total bytes.
      if (left.second.second > right.second.second) {
        return true;
      }
      if (left.second.second < right.second.second) {
        return false;
      }
      // In case of matching total bytes, sort by decreasing
      // total number of ranges.
      if (left.second.first > right.second.first) {
        return true;
      }
      if (left.second.first < right.second.first) {
        return false;
      }
      // in case of matching # bytes and #ranges, sort by increasing lexical
      // order of usage category.

      return left.first < right.first;
    }
  };

 public:
  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, _tallyDescriptor);
    UseTallies useTallies;
    for (const auto& range : _ranges) {
      const char* regionUse = range._value;
      typename std::map<const char*, std::pair<Offset, Offset> >::iterator
          itUse = useTallies.find(regionUse);
      if (itUse == useTallies.end()) {
        useTallies[regionUse] = std::make_pair(1, range._size);
      } else {
        itUse->second.first += 1;
        itUse->second.second += range._size;
      }
      tally.AdjustTally(range._size);
    }
    std::set<UseTalliesValue, Compare> sorted(useTallies.begin(),
                                              useTallies.end());

    /*
     * Show range information in decreasing order of bytes used, resolving
     * ties by decreasing order of number of ranges, resolving ties by
     * increasing lexical order of usage.
     */
    for (const auto& useAndTallies : sorted) {
      output << std::dec << useAndTallies.second.first << " ranges take 0x"
             << std::hex << useAndTallies.second.second
             << " bytes for use: " << useAndTallies.first << "\n";
    }
  }

 private:
  const std::string _helpMessage;
  const std::string _tallyDescriptor;
  const ClaimedRanges& _ranges;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
