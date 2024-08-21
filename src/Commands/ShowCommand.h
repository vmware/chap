// Copyright (c) 2017 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "SetBasedCommand.h"
namespace chap {
namespace Commands {
class ShowCommand : public SetBasedCommand {
 public:
  ShowCommand() : _name("show") {}
  void ShowHelpMessage(Context& context) {
    Output& output = context.GetOutput();
    output << "\nThe \"show\" command shows the contents of each member"
              " of a set.\n\n";
    SetBasedCommand::ShowHelpMessage(context);
  }
  const std::string& GetName() const { return _name; }

 private:
  const std::string _name;
};
}  // namespace Commands
}  // namespace chap
