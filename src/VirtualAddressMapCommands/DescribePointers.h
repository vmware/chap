// Copyright (c) 2019-2023 Broadcom. All Rights Reserved.
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
class DescribePointers : public Commands::Subcommand {
 public:
  typedef VirtualAddressMap<Offset> AddressMap;
  DescribePointers(const ProcessImage<Offset>& processImage,
                   const CompoundDescriber<Offset>& describer)
      : Commands::Subcommand("describe", "pointers"),
        _processImage(processImage),
        _addressMap(processImage.GetVirtualAddressMap()),
        _describer(describer) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << "Use \"describe pointers <address>\" to describe "
                           "all pointer-aligned addresses\nthat point "
                           "to the given address.\n";
  }

  void Run(Commands::Context& context) {
    Offset valueToMatch;
    bool hasErrors = false;
    if (context.GetNumPositionals() != 3 ||
        !context.ParsePositional(2, valueToMatch)) {
      hasErrors = true;
    }

    AddressFilter<Offset> addressFilter(_processImage, context);
    if (addressFilter.HasErrors()) {
      hasErrors = true;
    }
    if (hasErrors) {
      context.GetError() << "Use \"describe pointers <address>\" to describe "
                            "all pointer-aligned addresses\nthat point "
                            "to the given address.\n";
      return;
    }
    Commands::Output& output = context.GetOutput();
    bool filterIsActive = addressFilter.IsActive();
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
            Offset pointerAddress =
                ((it.Base()) + ((const char*)nextCandidate - rangeImage));
            if (filterIsActive && addressFilter.Exclude(pointerAddress)) {
              continue;
            }
            _describer.Describe(context, pointerAddress, false, true);
            output << "\n";
          }
        }
      }
    }
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const AddressMap& _addressMap;
  const CompoundDescriber<Offset>& _describer;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
