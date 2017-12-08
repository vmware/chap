// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "Runner.h"
#include "Subcommand.h"
namespace chap {
namespace Commands {
class SetBasedCommand : public Command {
 public:
  SetBasedCommand() {}

  void AddSubcommand(Subcommand& subcommand) {
    const std::string& commandName = subcommand.GetCommandName();
    const std::string& setName = subcommand.GetSetName();
    if (commandName != GetName()) {
      std::cerr << "Attempted to register \"" << commandName << " " << setName
                << "\" as subcommand of \"" << GetName() << "\".\n";
      return;
    }
    if (!_subcommands.insert(std::make_pair(setName, &subcommand)).second) {
      std::cerr << "Attempted to register subcommand " << GetName() << " "
                << setName << "\" more than once.\n";
    }
  }

  void Run(Context& context) {
    Error& error = context.GetError();
    if (_subcommands.empty()) {
      error << "There are no defined sets to " << GetName() << ".\n";
      return;
    }
    size_t numPositionals = context.GetNumPositionals();
    if (numPositionals == 1) {
      // TODO: add logic here to pick up previous arguments.
      error << "It is not clear what to " << GetName() << "\n";
      return;
    }
    const std::string& setName = context.Positional(1);
    std::map<std::string, Subcommand*>::iterator it =
        _subcommands.find(setName);
    if (it == _subcommands.end()) {
      error << "It is currently not defined how to " << GetName() << " "
            << setName << ".\n";
    } else {
      it->second->Run(context);
    }
  }

  void ShowAvailableSets(Context& context) {
    Output& output = context.GetOutput();
    if (_subcommands.empty()) {
      output << "There are currently no sets to " << GetName() << ".\n";
    } else {
      output << "It is possible to " << GetName()
             << " the following kinds of sets:\n";
      for (std::map<std::string, Subcommand*>::const_iterator it =
               _subcommands.begin();
           it != _subcommands.end(); ++it) {
        output << it->first << "\n";
      }
      output << "Try \"help " << GetName()
             << " <setname>\" for more"
                " information.\n";
    }
  }

  void ShowHelpMessage(Context& context) {
    size_t numPositionals = context.GetNumPositionals();
    if (numPositionals >= 3) {
      const std::string& setName = context.Positional(2);
      std::map<std::string, Subcommand*>::iterator it =
          _subcommands.find(setName);
      if (it == _subcommands.end()) {
        context.GetOutput() << "No help is available for \"" << GetName() << " "
                            << setName << "\".\n";
      } else {
        it->second->ShowHelpMessage(context);
        return;
      }
    }
    ShowAvailableSets(context);
  }

  virtual void GetSecondTokenCompletions(
      const std::string& prefix,
      std::function<void(const std::string&)> cb) const override {
    for (const auto& sc : _subcommands) {
      const std::string& scname = sc.first;
      if (!scname.compare(0, prefix.size(), prefix)) {
        cb(scname.c_str());
      }
    }
  }

 private:
  std::map<std::string, Subcommand*> _subcommands;
};

}  // namespace Commands
}  // namespace chap
