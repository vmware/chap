// Copyright (c) 2018-2023 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../ModuleDirectory.h"
#include "../SizedTally.h"
namespace chap {
namespace ModuleCommands {
template <class Offset>
class ListModules : public Commands::Subcommand {
 public:
  ListModules(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("list", "modules"),
        _moduleDirectory(processImage.GetModuleDirectory()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command lists the modules and their address ranges.\n";
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, "modules");
    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      Offset totalBytesForModule = 0;
      output << nameAndModuleInfo.first << " uses the following ranges:\n";
      for (const auto& range : nameAndModuleInfo.second._ranges) {
        totalBytesForModule += range._size;
        output << "[0x" << std::hex << range._base << ", 0x" << range._limit
               << ")\n";
      }
      tally.AdjustTally(totalBytesForModule);
    }
  }

 private:
  const ModuleDirectory<Offset>& _moduleDirectory;
};
}  // namespace ModuleCommands
}  // namespace chap
