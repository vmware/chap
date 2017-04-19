// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Commands/Runner.h"

namespace chap {
template <typename Offset>
class Explainer {
  /*
   * If the address is understood, provide an explanation for the address,
   * with output as specified and return true.  Otherwise don't write anything
   * and return false.
   */
  virtual bool Explain(Commands::Context& context,
                       Offset addressToExplain) const = 0;
};
}  // namespace chap
