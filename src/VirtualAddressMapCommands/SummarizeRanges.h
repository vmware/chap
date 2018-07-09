// Copyright (c) 2018 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <map>
#include <vector>
#include <algorithm>
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

 private:
  typedef std::map<const char*, std::pair<Offset, Offset> > UseTallies;
  typedef typename UseTallies::value_type UseTalliesValue;
  struct Compare {
    bool operator()(const UseTalliesValue& left,
                    const UseTalliesValue& right) {
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
      // order of usage.

      std::string leftString("unknown");
      if (left.first != (const char *)(0)) {
        leftString.assign(left.first);
      }
      std::string rightString("unknown");
      if (right.first != (const char *)(0)) {
        rightString.assign(right.first);
      }
      return leftString < rightString;
    }
  };
protected:
  void VisitRanges(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, _tallyDescriptor);
    typename PermissionsConstrainedRanges<Offset>::const_iterator itEnd =
        RangesSubcommand<Offset>::_ranges->end();
    UseTallies useTallies;
    for (typename PermissionsConstrainedRanges<Offset>::const_iterator it =
             RangesSubcommand<Offset>::_ranges->begin();
         it != itEnd; ++it) {
      const char* regionUse = it->_value;
      if (regionUse == (const char *)(0)) {
        regionUse = "unknown";
      }
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
    std::set<UseTalliesValue, Compare> sorted(useTallies.begin(),
                                              useTallies.end());

    /*
     * Show range information in decreasing order of bytes used, resolving
     * ties by decreasing order of number of ranges, resolving ties by
     * increasing lexical order of usage.
     */
    for (const auto& useAndTallies: sorted) {
      output << std::dec << useAndTallies.second.first << " ranges take 0x"
             << std::hex << useAndTallies.second.second
             << " bytes for use: " << useAndTallies.first << "\n";
    }
  }

 private:
  const std::string _tallyDescriptor;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
