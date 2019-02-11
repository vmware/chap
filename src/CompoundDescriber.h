// Copyright (c) 2017,2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <vector>
#include "Describer.h"

namespace chap {
template <typename Offset>
class CompoundDescriber : public Describer<Offset> {
 public:
  CompoundDescriber() {}
  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context &context, Offset addressToDescribe,
                bool explain, bool showAddresses) const {
    for (auto describer : _describers) {
      if (describer->Describe(context, addressToDescribe, explain,
                              showAddresses)) {
        return true;
      }
    }
    return false;
  }

  void AddDescriber(const Describer<Offset> &describer) {
    _describers.push_back(&describer);
  }

 private:
  std::vector<const Describer<Offset> *> _describers;
};
}  // namespace chap
