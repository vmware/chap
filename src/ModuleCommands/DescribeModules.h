// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../ModuleDirectory.h"
#include "../SizedTally.h"
namespace chap {
namespace ModuleCommands {
template <class Offset>
class DescribeModules : public Commands::Subcommand {
 public:
  DescribeModules(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("describe", "modules"),
        _moduleDirectory(processImage.GetModuleDirectory()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command describes the modules and their address ranges.\n";
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, "modules");
    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      Offset totalBytesForModule = 0;
      output << nameAndModuleInfo.first;
      const auto& moduleInfo = nameAndModuleInfo.second;
      if (moduleInfo._originalPath == moduleInfo._relocatedPath) {
        output << " has a corresponding file at that location.\n";
      } else {
        output << " has no corresponding file at that location.\n";
        if (!moduleInfo._relocatedPath.empty()) {
          output << " However, there is a file at " << moduleInfo._relocatedPath
                 << ".\n";
        }
      }
      output << " It uses the following ranges:\n";
      for (const auto& range : moduleInfo._ranges) {
        totalBytesForModule += range._size;
        output << " [0x" << std::hex << range._base << ", 0x" << range._limit
               << ") has flags 0x" << range._value._flags << "\n";
      }
      tally.AdjustTally(totalBytesForModule);
    }
  }

 private:
  const ModuleDirectory<Offset>& _moduleDirectory;
};
}  // namespace ModuleCommands
}  // namespace chap
