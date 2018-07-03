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
class ListRanges : public RangesSubcommand<Offset> {
 public:
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef const PermissionsConstrainedRanges<Offset>& (
      ProcessImage<Offset>::*RangesAccessor)() const;
  ListRanges(const std::string& subcommandName, const std::string& helpMessage,
             const std::string& tallyDescriptor, RangesAccessor rangesAccessor)
      : RangesSubcommand<Offset>("list", subcommandName, helpMessage,
                                 rangesAccessor),
        _tallyDescriptor(tallyDescriptor) {}
  void VisitRanges(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, _tallyDescriptor);
    typename PermissionsConstrainedRanges<Offset>::const_iterator itEnd =
        RangesSubcommand<Offset>::_ranges->end();
    for (typename PermissionsConstrainedRanges<Offset>::const_iterator it =
             RangesSubcommand<Offset>::_ranges->begin();
         it != itEnd; ++it) {
      tally.AdjustTally(it->_size);
      output << "Range [0x" << std::hex << it->_base << ", 0x" << it->_limit
             << ") uses 0x" << it->_size << " bytes.\n";
    }
  }

 private:
  const std::string _tallyDescriptor;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
