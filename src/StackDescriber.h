// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class StackDescriber : public Describer<Offset> {
 public:
  StackDescriber(const ProcessImage<Offset> *processImage) {
    SetProcessImage(processImage);
  }

  void SetProcessImage(const ProcessImage<Offset> *processImage) {
    if (processImage == 0) {
      _threadMap = 0;
    } else {
      _threadMap = &(processImage->GetThreadMap());
    }
  }

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.
   */
  bool Describe(Commands::Context &context, Offset address,
                bool explain) const {
    if (_threadMap == 0) {
      return false;
    }
    const typename ThreadMap<Offset>::ThreadInfo *threadInfo =
        _threadMap->find(address);
    if (threadInfo == NULL) {
      return false;
    }

    Commands::Output &output = context.GetOutput();
    output << "Address 0x" << std::hex << address << " is on the "
           << ((address >= threadInfo->_stackPointer) ? "live" : "dead")
           << " part of the stack for thread " << std::dec
           << threadInfo->_threadNum << ".\n";
    if (explain) {
      /*
       * At some point this should attempt to pin-point which frame
       * is involved and such.  This logic will be environment-specific.
       * For example on Linux it might be to walk the .eh_frame section.
       */
    }
    return true;
  }

 protected:
  const ThreadMap<Offset> *_threadMap;
};
}  // namespace chap
