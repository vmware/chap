// Copyright (c) 2017,2020,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../../SizedTally.h"
#include "../Describer.h"
#include "../Directory.h"
namespace chap {
namespace Allocations {
namespace Visitors {
template <class Offset>
class Describer {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  class Factory {
   public:
    Factory(const chap::Allocations::Describer<Offset>& describer)
        : _describer(describer), _commandName("describe") {}
    Describer* MakeVisitor(Commands::Context& context,
                           const ProcessImage<Offset>& processImage) {
      Offset showUpTo = 0;
      bool showAscii = false;
      bool switchError = false;
      size_t numShowUpToArguments = context.GetNumArguments("showUpTo");
      for (size_t i = 0; i < numShowUpToArguments; i++) {
        // We really expect at most one.  Pick the largest if not, but
        // require any arguments to be well formed.
        Offset upTo;
        if (!context.ParseArgument("showUpTo", i, upTo)) {
          switchError = true;
        } else {
          if (showUpTo < upTo) {
            showUpTo = upTo;
          }
        }
      }
      size_t numShowAsciiArguments = context.GetNumArguments("showAscii");
      if (numShowAsciiArguments > 0) {
        if (numShowUpToArguments == 0) {
          context.GetError()
              << "The /showAscii switch is allowed only if /showUpTo is set.\n";
          switchError = true;
        }
        if (!context.ParseBooleanSwitch("showAscii", showAscii)) {
          switchError = true;
        }
      }
      if (switchError) {
        return nullptr;
      }
      return new Describer(context, _describer,
                           processImage.GetVirtualAddressMap(), showUpTo,
                           showAscii);
    }
    const std::string& GetCommandName() const { return _commandName; }
    // TODO: allow adding taints
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "In this case \"describe\" means show the address, size,"
                "anchored/leaked/free\n"
                "status and type if known.\n";
    }

   private:
    const Allocations::Describer<Offset>& _describer;
    const std::string _commandName;
    const std::vector<std::string> _taints;
  };

  Describer(Commands::Context& context,
            const Allocations::Describer<Offset>& describer,
            const VirtualAddressMap<Offset>& addressMap, Offset showUpTo,
            bool showAscii)
      : _context(context),
        _describer(describer),
        _addressMap(addressMap),
        _showUpTo(showUpTo),
        _showAscii(showAscii),
        _sizedTally(context, "allocations") {}
  void Visit(AllocationIndex index, const Allocation& allocation) {
    size_t size = allocation.Size();
    _sizedTally.AdjustTally(size);
    _describer.Describe(_context, index, allocation, false, 0, false);
    if (_showUpTo > 0) {
      Commands::Output& output = _context.GetOutput();
      Offset numToShow = (size < _showUpTo) ? size : _showUpTo;
      const char* image;
      Offset numBytesFound =
          _addressMap.FindMappedMemoryImage(allocation.Address(), &image);
      if (numBytesFound < numToShow) {
        // This is not expected to happen on Linux.
        output << "Note that allocation is not contiguously mapped.\n";
        numToShow = numBytesFound;
      }
      output.HexDump((const Offset*)image, numToShow, _showAscii);
    }
  }

 private:
  Commands::Context& _context;
  const Allocations::Describer<Offset>& _describer;
  const VirtualAddressMap<Offset>& _addressMap;
  Offset _showUpTo;
  bool _showAscii;
  SizedTally<Offset> _sizedTally;
};
}  // namespace Visitors
}  // namespace Allocations
}  // namespace chap
