// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../ThreadMap.h"
namespace chap {
namespace ThreadMapCommands {
template <class Offset>
class ListStacks : public Commands::Subcommand {
 public:
  ListStacks() : Commands::Subcommand("list", "stacks"), _processImage(0) {}

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    if (processImage != NULL) {
      _threadMap = &processImage->GetThreadMap();
    } else {
      _threadMap = (const ThreadMap<Offset>*)(0);
    }
  }

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command lists the stacks for every thread and provides "
           "totals of the\nnumber of threads and the space they occupy.\n";
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
    typename ThreadMap<Offset>::const_iterator itEnd = _threadMap->end();
    for (typename ThreadMap<Offset>::const_iterator it = _threadMap->begin();
         it != itEnd; ++it) {
      output << "Thread " << std::dec << it->_threadNum
             << " uses stack block [0x" << std::hex << it->_stackBase << ", "
             << it->_stackLimit << ") current sp: 0x" << it->_stackPointer
             << "\n";
      totalBytes += (it->_stackLimit - it->_stackBase);
    }
    output << std::dec << _threadMap->NumThreads() << " threads use 0x"
           << std::hex << totalBytes << " bytes.\n";
  }

 private:
  const ProcessImage<Offset>* _processImage;
  const ThreadMap<Offset>* _threadMap;
};
}  // namespace ThreadMapCommands
}  // namespace chap
