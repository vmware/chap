// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../VirtualAddressMap.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class DescribeRangeRefs : public Commands::Subcommand {
 public:
  typedef VirtualAddressMap<Offset> AddressMap;
  DescribeRangeRefs(const AddressMap& addressMap,
                    const CompoundDescriber<Offset>& describer)
      : Commands::Subcommand("describe", "rangerefs"),
        _addressMap(addressMap),
        _describer(describer) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << "Use \"describe rangerefs <start> <limit>\" to "
                           "describe all\npointer-aligned addresses outside "
                           "of the range [start,limit) that contain a\npointer "
                           "to an address in that range.\n";
  }

  void Run(Commands::Context& context) {
    Offset rangeStart;
    Offset rangeLimit;
    if (context.GetNumTokens() != 4 || !context.ParseTokenAt(2, rangeStart) ||
        !context.ParseTokenAt(3, rangeLimit) || (rangeStart >= rangeLimit)) {
      context.GetError()
          << "Use \"describe rangerefs <start> <limit>\" to "
             "describe all\npointer-aligned addresses outside "
             "of the range [start,limit) that contain a\npointer "
             "to an address in that range.\n";
      return;
    }
    Commands::Output& output = context.GetOutput();
    typename AddressMap::const_iterator itEnd = _addressMap.end();
    for (typename AddressMap::const_iterator it = _addressMap.begin();
         it != itEnd; ++it) {
      const char* rangeImage = it.GetImage();
      if (rangeImage != (const char*)0) {
        const Offset* nextCandidate = (const Offset*)(rangeImage);
        Offset numCandidates = it.Size() / sizeof(Offset);

        for (const Offset* limit = nextCandidate + numCandidates;
             nextCandidate < limit; nextCandidate++) {
          Offset maybeInRange = *nextCandidate;
          if (maybeInRange >= rangeStart && maybeInRange < rangeLimit) {
            Offset refAddr =
                it.Base() + ((const char*)nextCandidate - rangeImage);
            if (refAddr < rangeStart || refAddr >= rangeLimit) {
              output << std::hex << refAddr << "\n";
              _describer.Describe(context, refAddr, false, true);
              output << "\n";
            }
          }
        }
      }
    }
  }

 private:
  const AddressMap& _addressMap;
  const CompoundDescriber<Offset>& _describer;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
