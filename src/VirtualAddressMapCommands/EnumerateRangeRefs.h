// Copyright (c) 2020-2023 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../VirtualAddressMap.h"
#include "AddressFilter.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class EnumerateRangeRefs : public Commands::Subcommand {
 public:
  typedef VirtualAddressMap<Offset> AddressMap;
  EnumerateRangeRefs(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("enumerate", "rangerefs"),
        _processImage(processImage),
        _addressMap(processImage.GetVirtualAddressMap()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << "Use \"enumerate rangerefs <start> <limit>\" to "
                           "enumerate all\npointer-aligned addresses outside "
                           "of the range [start,limit) that contain a\npointer "
                           "to an address in that range.\n";
  }

  void Run(Commands::Context& context) {
    Offset rangeStart;
    Offset rangeLimit;
    bool hasErrors = false;
    if (context.GetNumPositionals() != 4 ||
        !context.ParsePositional(2, rangeStart) ||
        !context.ParsePositional(3, rangeLimit) || (rangeStart >= rangeLimit)) {
      hasErrors = true;
    }
    AddressFilter<Offset> addressFilter(_processImage, context);
    if (addressFilter.HasErrors()) {
      hasErrors = true;
    }
    if (hasErrors) {
      context.GetError()
          << "Use \"enumerate rangerefs <start> <limit>\" to "
             "enumerate all\npointer-aligned addresses outside "
             "of the range [start,limit) that contain a\npointer "
             "to an address in that range.\n";
      return;
    }
    Commands::Output& output = context.GetOutput();
    bool filterIsActive = addressFilter.IsActive();
    output << std::hex;
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
              if (filterIsActive && addressFilter.Exclude(refAddr)) {
                continue;
              }
              output << std::hex << refAddr << "\n";
            }
          }
        }
      }
    }
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const AddressMap& _addressMap;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
