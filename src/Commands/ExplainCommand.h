// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "../Describer.h"
#include "SetBasedCommand.h"
namespace chap {
namespace Commands {
template <class Offset>
class ExplainCommand : public SetBasedCommand {
 public:
  ExplainCommand(const Describer<Offset>& describer)
      : _name("explain"), _describer(describer) {}
  void ShowHelpMessage(Context& context) {
    Output& output = context.GetOutput();
    output << "\nThe \"explain\" command describes the use of the specified "
              "address or of the\n"
              "members of the specified set and explains the reasons for "
              "the given description.\n\n";
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
        if (!_describer.Describe(context, address, true)) {
          error << "Currently no explanation is available for address 0x"
                << std::hex << address << "\n";
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
