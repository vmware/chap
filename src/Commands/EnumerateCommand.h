// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "SetBasedCommand.h"
namespace chap {
namespace Commands {
class EnumerateCommand : public SetBasedCommand {
 public:
  EnumerateCommand() : _name("enumerate") {}
  void ShowHelpMessage(Context& context) {
    Output& output = context.GetOutput();
    output << "\nThe \"enumerate\" command provides an identifier,"
              " typically the start address, for\neach member of a set.\n\n";
    SetBasedCommand::ShowHelpMessage(context);
  }
  const std::string& GetName() const { return _name; }

 private:
  const std::string _name;
};
}  // namespace Commands
}  // namespace chap
