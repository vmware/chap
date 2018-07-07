// Copyright (c) 2018 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../PermissionsConstrainedRanges.h"
#include "../ProcessImage.h"
#include "../SizedTally.h"
#include "RangesSubcommand.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class SummarizeRanges : public RangesSubcommand<Offset> {
 public:
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef const PermissionsConstrainedRanges<Offset>& (
      ProcessImage<Offset>::*RangesAccessor)() const;
  SummarizeRanges(const std::string& subcommandName,
                  const std::string& helpMessage,
                  const std::string& tallyDescriptor,
                  RangesAccessor rangesAccessor)
      : RangesSubcommand<Offset>("summarize", subcommandName, helpMessage,
                                 rangesAccessor),
        _tallyDescriptor(tallyDescriptor) {}
  void VisitRanges(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, _tallyDescriptor);
    typename PermissionsConstrainedRanges<Offset>::const_iterator itEnd =
        RangesSubcommand<Offset>::_ranges->end();
    std::map<const char*, std::pair<Offset, Offset> > useTallies;
    for (typename PermissionsConstrainedRanges<Offset>::const_iterator it =
             RangesSubcommand<Offset>::_ranges->begin();
         it != itEnd; ++it) {
      const char* regionUse = it->_value;
      typename std::map<const char*, std::pair<Offset, Offset> >::iterator
          itUse = useTallies.find(regionUse);
      if (itUse == useTallies.end()) {
        useTallies[regionUse] = std::make_pair(1, it->_size);
      } else {
        itUse->second.first += 1;
        itUse->second.second += it->_size;
      }
      tally.AdjustTally(it->_size);
    }
    /*
     * For now just dump the summary in order of usage type because the number
     * of usage types is sufficiently small that there is no point sorting it.
     */
    for (const auto& useAndTallies : useTallies) {
      const char *usage = useAndTallies.first;
      if (usage == (const char *)(0)) {
        usage = "unknown";
      }
      output << std::dec << useAndTallies.second.first << " ranges take 0x"
             << std::hex << useAndTallies.second.second
             << " bytes for use: " << usage << "\n";
    }
  }

 private:
  const std::string _tallyDescriptor;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
