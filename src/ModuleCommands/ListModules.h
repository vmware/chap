// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../ModuleDirectory.h"
namespace chap {
namespace ModuleCommands {
template <class Offset>
class ListModules : public Commands::Subcommand {
 public:
  ListModules() : Commands::Subcommand("list", "modules"), _processImage(0) {}

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    if (processImage != NULL) {
      _moduleDirectory = &processImage->GetModuleDirectory();
    } else {
      _moduleDirectory = (const ModuleDirectory<Offset>*)(0);
    }
  }

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command lists the modules and their address ranges.\n";
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    Commands::Error& error = context.GetError();
    bool isRedirected = context.IsRedirected();
    if (_processImage == 0) {
      error << "This command is currently disabled.\n";
      error << "There is no process image.\n";
      if (isRedirected) {
        output << "This command is currently disabled.\n";
        output << "There is no process image.\n";
      }
      return;
    }
    Offset totalBytes = 0;
    typename ModuleDirectory<Offset>::const_iterator itEnd = _moduleDirectory->end();
    for (typename ModuleDirectory<Offset>::const_iterator it =
             _moduleDirectory->begin();
         it != itEnd; ++it) {
      const std::string& moduleName = it->first;
      const RangeMapper<Offset, Offset>& ranges = it->second;
      typename RangeMapper<Offset, Offset>::const_iterator itRangeEnd =
          ranges.end();
      output << moduleName << " uses the following ranges:\n";
      for (typename RangeMapper<Offset, Offset>::const_iterator itRange =
               ranges.begin();
           itRange != itRangeEnd; ++itRange) {
        totalBytes += itRange->_size;
        output << "[0x" << std::hex << itRange->_base << ", 0x"
                  << itRange->_limit << ")\n";
      }
    }
    output << std::dec << _moduleDirectory->NumModules() << " modules use 0x"
           << std::hex << totalBytes << " bytes.\n";
  }

 private:
  const ProcessImage<Offset>* _processImage;
  const ModuleDirectory<Offset>* _moduleDirectory;
};
}  // namespace ModuleCommands
}  // namespace chap
