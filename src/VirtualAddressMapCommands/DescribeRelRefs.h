// Copyright (c) 2019,2025 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../VirtualAddressMap.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class DescribeRelRefs : public Commands::Subcommand {
 public:
  typedef VirtualAddressMap<Offset> AddressMap;
  DescribeRelRefs(const AddressMap& addressMap,
                  const CompoundDescriber<Offset>& describer)
      : Commands::Subcommand("describe", "relrefs"),
        _addressMap(addressMap),
        _describer(describer) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << "Use \"describe relrefs <address>\" to describe "
                           "all  addresses that contain a signed\n32-bit "
                           "integer that, when added to the address just after "
                           "the integer, yields the\nrequested address.\n";
  }

  void Run(Commands::Context& context) {
    Offset valueToMatch;
    if (context.GetNumTokens() != 3 || !context.ParseTokenAt(2, valueToMatch)) {
      context.GetError()
          << "Use \"describe relrefs <address>\" to describe "
             "all  addresses that contain a signed\n32-bit "
             "integer that, when added to the address just after "
             "the integer, yields the\nrequested address.\n";
      return;
    }
    Commands::Output& output = context.GetOutput();
    typename AddressMap::const_iterator itEnd = _addressMap.end();
    for (typename AddressMap::const_iterator it = _addressMap.begin();
         it != itEnd; ++it) {
      const char* rangeImage = it.GetImage();
      if (rangeImage != (const char*)0) {
        const unsigned char* nextCandidate = (const unsigned char*)(rangeImage);

        Offset addr = it.Base();
        Offset valueToMatchMinusSizeofInt = valueToMatch - sizeof(int);
        for (const unsigned char* limit =
                 nextCandidate + it.Size() - sizeof(int) + 1;
             nextCandidate < limit; nextCandidate++) {
          if (addr == valueToMatchMinusSizeofInt) {
            addr++;
            continue;
          }
          if (addr + *(int*)(nextCandidate) == valueToMatchMinusSizeofInt) {
            output << std::hex << addr << "\n";
            _describer.Describe(context, addr, false, true);
            output << "\n";
          }
          addr++;
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
