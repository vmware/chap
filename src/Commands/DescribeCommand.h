// Copyright (c) 2017,2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "../Describer.h"
#include "SetBasedCommand.h"
namespace chap {
namespace Commands {
template <class Offset>
class DescribeCommand : public SetBasedCommand {
 public:
  DescribeCommand(const Describer<Offset>& describer)
      : _name("describe"), _describer(describer) {}
  void ShowHelpMessage(Context& context) {
    Output& output = context.GetOutput();
    output << "\nThe \"describe\" command describes the use of the specified "
              "address or of the\n"
              "members of the specified set.\n\n";
    SetBasedCommand::ShowHelpMessage(context);
  }
  const std::string& GetName() const { return _name; }

  void Run(Context& context) {
    size_t numPositionals = context.GetNumPositionals();
    if (numPositionals > 1) {
      uint64_t address;
      if (context.ParsePositional(1, address)) {
        Error& error = context.GetError();
        if (numPositionals != 2) {
          error << "If an address is specified, only one is allowed.\n";
          return;
        }
        if (!_describer.Describe(context, address, false, true)) {
          /*
           * Generally the describer will provide at least a minimal
           * description if the address is at all known in the process
           * image.
           */
          error << "0x" << std::hex << address
                << " is probably not a valid address.\n";
        }
        return;
      }
    }
    SetBasedCommand::Run(context);
  }

 private:
  const std::string _name;
  const Describer<Offset>& _describer;
};
}  // namespace Commands
}  // namespace chap
