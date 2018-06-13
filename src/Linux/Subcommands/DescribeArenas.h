// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../LibcMallocAllocationFinder.h"
#include "../../SizedTally.h"
namespace chap {
namespace Linux {
namespace Subcommands {
template <class Offset>
class DescribeArenas : public Commands::Subcommand {
 public:
  DescribeArenas()
      : Commands::Subcommand("describe", "arenas"), _processImage(0) {}

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    if (processImage != NULL) {
      _allocationFinder =
          dynamic_cast<const LibcMallocAllocationFinder<Offset>*>(
              processImage->GetAllocationFinder());
    } else {
      _allocationFinder = (const LibcMallocAllocationFinder<Offset>*)(0);
    }
  }

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This subcommand describes all the arenas associated "
           "with libc malloc.\n";
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
    if (_allocationFinder == 0) {
      error << "This command is currently disabled.\n";
      error << "The process didn't use libc malloc.\n";
      if (isRedirected) {
        output << "This command is currently disabled.\n";
        output << "The process didn't use libc malloc.\n";
      }
      return;
    }
    SizedTally<Offset> tally(context, "arenas");
    const typename LibcMallocAllocationFinder<Offset>::ArenaMap& arenaMap =
        _allocationFinder->GetArenas();
    for (const auto& addressAndInfo : arenaMap) {
      Offset address = addressAndInfo.first;
      const typename LibcMallocAllocationFinder<Offset>::Arena arena =
          addressAndInfo.second;
      tally.AdjustTally(arena._size);
      output << "Arena at 0x" << std::hex << address << " has size 0x"
             << arena._size << ".\n"
             << std::dec << arena._freeCount << " free allocations take 0x"
             << std::hex << arena._freeBytes << " bytes.\n"
             << std::dec << arena._usedCount << " used allocations take 0x"
             << std::hex << arena._usedBytes << " bytes.\n\n";
    }
  }

 private:
  const ProcessImage<Offset>* _processImage;
  const LibcMallocAllocationFinder<Offset>* _allocationFinder;
};
}  // namespace Subcommands
}  // namespace Linux
}  // namespace chap
