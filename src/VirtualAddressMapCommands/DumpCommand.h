// Copyright (c) 2017,2019-2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "../Commands/Runner.h"
#include "../VirtualAddressMap.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class DumpCommand : public Commands::Command {
 public:
  DumpCommand(const VirtualAddressMap<Offset>& virtualAddressMap)
      : _name("dump"), _virtualAddressMap(virtualAddressMap) {}
  void ShowHelpMessage(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    output << "Use \"dump <address-in-hex> <size-in-hex>\" to dump the "
              "specified range.\n"
              "At present the only available format is as pointer-size "
              "values.\n\n";
  }
  const std::string& GetName() const { return _name; }

  void Run(Commands::Context& context) {
    size_t numPositionals = context.GetNumPositionals();
    uint64_t address;
    uint64_t size;
    Commands::Error& error = context.GetError();
    bool argsOK = true;
    if (numPositionals == 3) {
      if (!context.ParsePositional(1, address)) {
        error << "Failed to parse address.\n";
        argsOK = false;
      }
      if (!context.ParsePositional(2, size)) {
        error << "Failed to parse size.\n";
        argsOK = false;
      }
    } else {
      argsOK = false;
    }
    bool showAscii = false;
    (void)context.ParseBooleanSwitch("showAscii", showAscii);
    if (argsOK) {
      const char* image;
      Offset numBytesFound =
          _virtualAddressMap.FindMappedMemoryImage(address, &image);
      if (numBytesFound < size) {
        error << "Only 0x" << std::hex << numBytesFound
              << " bytes were mapped starting from that address\n";
        size = numBytesFound;
      }
      context.GetOutput().HexDump((const Offset*)image, size, showAscii);
    } else {
      error << "Use \"dump <address-in-hex> <size-in-hex>\" to dump the "
               "specified range.\n";
    }
  }

 private:
  const std::string _name;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
};
}  // namespace Commands
}  // namespace chap
