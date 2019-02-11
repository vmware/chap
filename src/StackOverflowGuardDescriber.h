// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class StackOverflowGuardDescriber : public Describer<Offset> {
  typedef typename VirtualMemoryPartition<Offset>::ClaimedRanges ClaimedRanges;

 public:
  StackOverflowGuardDescriber(const ProcessImage<Offset> &processImage)
      : _processImage(processImage),
        _threadMap(processImage.GetThreadMap()),
        _virtualAddressMap(processImage.GetVirtualAddressMap()),
        _virtualMemoryPartition(processImage.GetVirtualMemoryPartition()),
        _inaccessibleRanges(
            _virtualMemoryPartition.GetClaimedInaccessibleRanges()),
        _readOnlyRanges(_virtualMemoryPartition.GetClaimedReadOnlyRanges()) {}

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context &context, Offset address, bool explain,
                bool showAddresses) const {
    bool foundAsReadOnly = false;
    typename ClaimedRanges::const_iterator it =
        _inaccessibleRanges.find(address);
    if (it == _inaccessibleRanges.end()) {
      it = _readOnlyRanges.find(address);
      if (it == _readOnlyRanges.end()) {
        return false;
      }
      foundAsReadOnly = true;
    }
    if (it->_value != _processImage.STACK_OVERFLOW_GUARD) {
      return false;
    }
    Offset guardBase = it->_base;
    Offset guardLimit = it->_limit;

    // TODO: There should be no assumption that the stack is currently
    // associated with a thread.

    Commands::Output &output = context.GetOutput();
    if (showAddresses) {
      output << "Address 0x" << std::hex << address << " is at offset 0x"
             << (address - guardBase) << " in stack overflow guard\n[0x"
             << guardBase << ", 0x" << guardLimit << ")\nfor ";
    } else {
      output << "This is a stack overflow guard for ";
    }
    const typename ThreadMap<Offset>::ThreadInfo *threadInfo =
        _threadMap.find(guardLimit);
    if (threadInfo == nullptr) {
      output << "some unknown stack.\n";
    } else {
      output << "the stack for thread " << std::dec << threadInfo->_threadNum
             << ".\n";
    }

    if (explain) {
      if (foundAsReadOnly) {
        output << "The guard is marked readable, likely due to a bug in "
                  "creation of the core.\n";
      } else {
        if (_virtualAddressMap.find(address) == _virtualAddressMap.end()) {
          output << "The guard is not listed in the core but is inferred "
                    "based on the adjacent ranges.\n";
        }
      }
    }
    return true;
  }

 private:
  const ProcessImage<Offset> &_processImage;
  const ThreadMap<Offset> &_threadMap;
  const VirtualAddressMap<Offset> &_virtualAddressMap;
  const VirtualMemoryPartition<Offset> &_virtualMemoryPartition;
  const ClaimedRanges &_inaccessibleRanges;
  const ClaimedRanges &_readOnlyRanges;
};
}  // namespace chap
