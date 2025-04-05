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
class EnumerateRelRefs : public Commands::Subcommand {
 public:
  typedef VirtualAddressMap<Offset> AddressMap;
  EnumerateRelRefs(const AddressMap& addressMap)
      : Commands::Subcommand("enumerate", "relrefs"), _addressMap(addressMap) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << "Use \"enumerate relrefs <address>\" to enumerate "
                           "all  addresses that contain a\nsigned 32-bit "
                           "integer that, when added to the address just after "
                           "the integer,\nyields the requested address.\n";
  }

  void Run(Commands::Context& context) {
    Offset valueToMatch;
    if (context.GetNumTokens() != 3 || !context.ParseTokenAt(2, valueToMatch)) {
      context.GetError()
          << "Use \"enumerate relrefs <address>\" to enumerate "
             "all  addresses that contain a\nsigned 32-bit "
             "integer that, when added to the address just after "
             "the integer,\nyields the requested address.\n";
      return;
    }
    Commands::Output& output = context.GetOutput();
    output << std::hex;
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
            continue;
          }
          if (addr + *(int*)(nextCandidate) == valueToMatchMinusSizeofInt) {
            output << std::hex << addr << "\n";
          }
          addr++;
        }
      }
    }
  }

 private:
  const AddressMap& _addressMap;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
