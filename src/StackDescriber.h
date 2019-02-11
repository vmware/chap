// Copyright (c) 2017,2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class StackDescriber : public Describer<Offset> {
 public:
  StackDescriber(const ProcessImage<Offset> &processImage)
      : _threadMap(processImage.GetThreadMap()) {}

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context &context, Offset address, bool explain,
                bool showAddresses) const {
    const typename ThreadMap<Offset>::ThreadInfo *threadInfo =
        _threadMap.find(address);
    if (threadInfo == NULL) {
      return false;
    }
    // TODO: There should be no assumption that the stack is currently
    // associated with a thread.

    Commands::Output &output = context.GetOutput();
    if (showAddresses) {
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
    } else {
      output << "This stack is used for thread " << std::dec
             << threadInfo->_threadNum << ".\n";
      if (explain) {
        /*
         * At some point this should explain who holds the thread, if that is
         * known.
         */
      }
    }
    return true;
  }

 protected:
  const ThreadMap<Offset> &_threadMap;
};
}  // namespace chap
