// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "SetBasedCommand.h"
namespace chap {
namespace Commands {
class ListCommand : public SetBasedCommand {
 public:
  ListCommand() : _name("list") {}
  void ShowHelpMessage(Context& context) {
    Output& output = context.GetOutput();
    output << "\nThe \"list\" command lists the members of a set,"
              " providing some simple information\nfor each member.\n\n";
    SetBasedCommand::ShowHelpMessage(context);
  }
  const std::string& GetName() const { return _name; }

 private:
  const std::string _name;
};
}  // namespace Commands
}  // namespace chap
