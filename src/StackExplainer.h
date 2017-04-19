// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Explainer.h"
#include "ThreadMap.h"

namespace chap {
template <typename Offset>
class StackExplainer : public Explainer<Offset> {
 public:
  StackExplainer() {}

  void SetThreadMap(const ThreadMap<Offset> *threadMap) {
    _threadMap = threadMap;
  }

  /*
   * If the address is understood, provide an explanation for the address,
   * with output as specified and return true.  Otherwise don't write anything
   * and return false.
   */
  virtual bool Explain(Commands::Context &context,
                       Offset addressToExplain) const {
    if (_threadMap != NULL) {
      Commands::Output &output = context.GetOutput();
      const typename ThreadMap<Offset>::ThreadInfo *threadInfo =
          _threadMap->find(addressToExplain);
      if (threadInfo != NULL) {
        output << "Address 0x" << std::hex << addressToExplain << " is on the "
               << ((addressToExplain >= threadInfo->_stackPointer) ? "live"
                                                                   : "dead")
               << " part of the stack for thread " << std::dec
               << threadInfo->_threadNum << ".\n";
        return true;
      }
    }
    return false;
  }

 private:
  const ThreadMap<Offset> *_threadMap;
};
}  // namespace chap
