// Copyright (c) 2017,2019,2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class StackDescriber : public Describer<Offset> {
 public:
  StackDescriber(const ProcessImage<Offset> &processImage)
      : _stackRegistry(processImage.GetStackRegistry()) {}

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context &context, Offset address, bool explain,
                bool showAddresses) const {
    Commands::Output &output = context.GetOutput();
    return _stackRegistry.VisitStack(
        address, [&output, address, explain, showAddresses](
                     Offset regionBase, Offset regionLimit,
                     const char *stackType, Offset stackTop,
                     Offset /*stackBase - not used yet*/, size_t threadNum) {

          if (showAddresses) {
            output << "Address 0x" << std::hex << address << " is in ";
            if (stackTop != StackRegistry<Offset>::STACK_TOP_UNKNOWN) {
              output << "the " << ((address >= stackTop) ? "live" : "dead")
                     << " part of ";
            }
            output << "the " << stackType << " that\nuses [0x" << std::hex
                   << regionBase << ", 0x" << regionLimit << ").\n";
            if (threadNum != StackRegistry<Offset>::THREAD_NUMBER_UNKNOWN) {
              output << "Thread " << std::dec << threadNum
                     << " is currently using this stack.\n";
            }
            if (explain) {
              /*
               * At some point this should attempt to pin-point which frame
               * is involved and such.  This logic will be environment-specific.
               * For example on Linux it might be to walk the .eh_frame section.
               */
            }
          } else {
            /*
             * The type of the stack will already be displayed by the range
             * description logic.  The following is just to add additional
             * descriptive information for the range.
             */
            if (threadNum != StackRegistry<Offset>::THREAD_NUMBER_UNKNOWN) {
              output << "This " << stackType << " is currently used by thread "
                     << std::dec << threadNum << ".\n";
            }

            if (explain) {
              /*
               * At some point this should explain who holds the thread, if that
               * is
               * known.
               */
            }
          }

          return true;  // This visit succeeded.
        });
  }

 protected:
  const StackRegistry<Offset> &_stackRegistry;
};
}  // namespace chap
