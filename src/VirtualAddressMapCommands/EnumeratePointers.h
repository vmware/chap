// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../VirtualAddressMap.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class EnumeratePointers : public Commands::Subcommand {
 public:
  typedef VirtualAddressMap<Offset> AddressMap;
  EnumeratePointers(const AddressMap& addressMap)
      : Commands::Subcommand("enumerate", "pointers"),
        _addressMap(addressMap) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << "Use \"enumerate pointers <address>\" to enumerate "
                           "all pointer-aligned addresses\nthat point "
                           "to the given address.\n";
  }

  void Run(Commands::Context& context) {
    Offset valueToMatch;
    if (context.GetNumTokens() != 3 || !context.ParseTokenAt(2, valueToMatch)) {
      context.GetError() << "Use \"enumerate pointers <address>\" to enumerate "
                            "all pointer-aligned addresses\nthat point "
                            "to the given address.\n";
      return;
    }
    Commands::Output& output = context.GetOutput();
    output << std::hex;
    typename AddressMap::const_iterator itEnd = _addressMap.end();
    for (typename AddressMap::const_iterator it = _addressMap.begin();
         it != itEnd; ++it) {
      Offset numCandidates = it.Size() / sizeof(Offset);
      const char* rangeImage = it.GetImage();
      if (rangeImage != (const char*)0) {
        const Offset* nextCandidate = (const Offset*)(rangeImage);

        for (const Offset* limit = nextCandidate + numCandidates;
             nextCandidate < limit; nextCandidate++) {
          if (*nextCandidate == valueToMatch) {
            output << ((it.Base()) + ((const char*)nextCandidate - rangeImage))
                   << "\n";
          }
        }
      }
    }
  }

 private:
  const AddressMap& _addressMap;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
