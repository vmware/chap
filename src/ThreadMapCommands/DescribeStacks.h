// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../ThreadMap.h"
#include "../SizedTally.h"
namespace chap {
namespace ThreadMapCommands {
template <class Offset>
class DescribeStacks : public Commands::Subcommand {
 public:
  DescribeStacks() : Commands::Subcommand("describe", "stacks"), _processImage(0) {}

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    if (processImage != NULL) {
      _threadMap = &processImage->GetThreadMap();
      _addressMap = &processImage->GetVirtualAddressMap();
    } else {
      _threadMap = (const ThreadMap<Offset>*)(0);
      _addressMap = (const VirtualAddressMap<Offset>*)(0);
    }
  }

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command describes the stacks for every thread and provides "
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
    SizedTally<Offset> tally(context, "stacks");
    typename ThreadMap<Offset>::const_iterator itEnd = _threadMap->end();
    typename VirtualAddressMap<Offset>::Reader reader(*_addressMap);
    for (typename ThreadMap<Offset>::const_iterator it = _threadMap->begin();
         it != itEnd; ++it) {
      Offset base = it->_stackBase;
      Offset limit = it->_stackLimit;
      Offset check0 = base;
      while (check0 < limit && reader.ReadOffset(check0, 0xbad) == 0) {
	check0 += sizeof(Offset);
      }
      Offset peakStackUsage = limit - check0;
      output << "Thread " << std::dec << it->_threadNum
             << " uses stack block [0x" << std::hex << base << ", "
             << limit << ")\n current sp: 0x" << it->_stackPointer
             << "\n";
      output << "Peak stack usage was 0x" << peakStackUsage
	     << " bytes out of 0x" << (limit - base) << " total.\n\n";
      tally.AdjustTally(limit - base);
    }
  }

 private:
  const ProcessImage<Offset>* _processImage;
  const ThreadMap<Offset>* _threadMap;
  const VirtualAddressMap<Offset>* _addressMap;
};
}  // namespace ThreadMapCommands
}  // namespace chap
