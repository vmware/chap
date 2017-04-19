// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "SetBasedCommand.h"
namespace chap {
namespace Commands {
class SummarizeCommand : public SetBasedCommand {
 public:
  SummarizeCommand() : _name("summarize") {}
  void ShowHelpMessage(Context& context) {
    Output& output = context.GetOutput();
    output << "\nThe \"summarize\" command provides summary information"
              " for the members of a set.\n\n";
    SetBasedCommand::ShowHelpMessage(context);
  }
  const std::string& GetName() const { return _name; }

 private:
  const std::string _name;
};
}  // namespace Commands
}  // namespace chap
