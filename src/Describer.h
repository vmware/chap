// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Commands/Runner.h"

namespace chap {
template <typename Offset>
class Describer {
 public:
  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.
   */
  virtual bool Describe(Commands::Context& context, Offset addressToDescribe,
                        bool explain) const = 0;
};
}  // namespace chap
