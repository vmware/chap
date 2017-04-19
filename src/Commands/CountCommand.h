// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "SetBasedCommand.h"
namespace chap {
namespace Commands {
class CountCommand : public SetBasedCommand {
 public:
  CountCommand() : _name("count") {}
  void ShowHelpMessage(Context& context) {
    Output& output = context.GetOutput();
    output << "\nThe \"count\" command reports the size of a set.\n"
              "It may also report some other aggregate value across members"
              "  of the set.\n\n";
    SetBasedCommand::ShowHelpMessage(context);
  }
  const std::string& GetName() const { return _name; }

 private:
  const std::string _name;
};
}  // namespace Commands
}  // namespace chap
