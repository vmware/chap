// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Explainer.h"
#include "ModuleDirectory.h"

namespace chap {
template <typename Offset>
class InModuleExplainer : public Explainer<Offset> {
 public:
  InModuleExplainer() {}

  void SetModuleDirectory(const ModuleDirectory<Offset>* moduleDirectory) {
    _moduleDirectory = moduleDirectory;
  }

  /*
   * If the address is understood, provide an explanation for the address,
   * with output as specified and return true.  Otherwise don't write anything
   * and return false.
   */
  virtual bool Explain(Commands::Context& context,
                       Offset addressToExplain) const {
    std::string name;
    Offset base;
    Offset size;
    if (_moduleDirectory != 0 &&
        _moduleDirectory->Find(addressToExplain, name, base, size)) {
      Commands::Output& output = context.GetOutput();
      output << "Address 0x" << std::hex << addressToExplain
             << " is at offset 0x" << (addressToExplain - base) << " in module "
             << name << " loaded at 0x" << base << ".\n";
      return true;
    }
    return false;
  }

 private:
  const ModuleDirectory<Offset>* _moduleDirectory;
};
}  // namespace chap
