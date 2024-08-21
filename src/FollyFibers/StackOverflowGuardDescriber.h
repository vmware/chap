// Copyright (c) 2021 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Describer.h"
#include "../ProcessImage.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace PThread {
template <typename Offset>
class StackOverflowGuardDescriber : public Describer<Offset> {
  typedef typename VirtualMemoryPartition<Offset>::ClaimedRanges ClaimedRanges;

 public:
  StackOverflowGuardDescriber(const ProcessImage<Offset> &processImage)
      : _processImage(processImage),
        _stackRegistry(processImage.GetStackRegistry()),
        _virtualAddressMap(processImage.GetVirtualAddressMap()),
        _virtualMemoryPartition(processImage.GetVirtualMemoryPartition()),
        _inaccessibleRanges(
            _virtualMemoryPartition.GetClaimedInaccessibleRanges()),
        _readOnlyRanges(_virtualMemoryPartition.GetClaimedReadOnlyRanges()),
        PTHREAD_STACK_OVERFLOW_GUARD(
            processImage.GetPThreadInfrastructureFinder()
                .PTHREAD_STACK_OVERFLOW_GUARD) {}

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
    if (it->_value != PTHREAD_STACK_OVERFLOW_GUARD) {
      return false;
    }
    Offset guardBase = it->_base;
    Offset guardLimit = it->_limit;
    Commands::Output &output = context.GetOutput();
    return _stackRegistry.VisitStack(
        guardLimit, [&](Offset regionBase, Offset regionLimit,
                        const char *stackType, Offset stackTop,
                        Offset /*stackBase - not used yet*/, size_t threadNum) {

          if (showAddresses) {
            // TODO: distinguish areas used for thread-local variables.
            output << "Address 0x" << std::hex << address << " is at offset 0x"
                   << (address - guardBase) << " in a "
                   << PTHREAD_STACK_OVERFLOW_GUARD << "\n[0x" << guardBase
                   << ", 0x" << guardLimit << ") for the " << stackType << "\n"
                   << "that uses [0x" << std::hex << regionBase << ", 0x"
                   << regionLimit << ").\n";
            if (threadNum != StackRegistry<Offset>::THREAD_NUMBER_UNKNOWN) {
              output << "Thread " << threadNum
                     << " is currently using that stack.\n";
            }
          } else {
            output << "This is a " << PTHREAD_STACK_OVERFLOW_GUARD << ".\n";
            output << "This is used for a " << stackType;
            if (threadNum != StackRegistry<Offset>::THREAD_NUMBER_UNKNOWN) {
              output << ", which is currently used by thread " << std::dec
                     << threadNum;
            }
            output << ".\n";
          }
          if (explain) {
            if (foundAsReadOnly) {
              output << "The guard is marked readable, likely due to a bug in "
                        "creation of the core.\n";
            } else {
              if (_virtualAddressMap.find(address) ==
                  _virtualAddressMap.end()) {
                output << "The guard is not listed in the core but is inferred "
                          "based on the adjacent ranges.\n";
              }
            }
          }

          return true;  // This visit succeeded.
        });
  }

 private:
  const ProcessImage<Offset> &_processImage;
  const StackRegistry<Offset> &_stackRegistry;
  const VirtualAddressMap<Offset> &_virtualAddressMap;
  const VirtualMemoryPartition<Offset> &_virtualMemoryPartition;
  const ClaimedRanges &_inaccessibleRanges;
  const ClaimedRanges &_readOnlyRanges;
  const char *PTHREAD_STACK_OVERFLOW_GUARD;
};
}  // namespace PThread
}  // namespace chap
