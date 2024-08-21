// Copyright (c) 2017 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "Runner.h"
namespace chap {
namespace Commands {
class Subcommand {
 public:
  Subcommand(const std::string& commandName, const std::string& setName)
      : _commandName(commandName), _setName(setName) {}

  virtual void Run(Commands::Context& context) = 0;

  virtual void ShowHelpMessage(Context& context) = 0;

  const std::string& GetCommandName() const { return _commandName; }

  const std::string& GetSetName() const { return _setName; }

 private:
  const std::string _commandName;
  const std::string _setName;
};

}  // namespace Commands
}  // namespace chap
