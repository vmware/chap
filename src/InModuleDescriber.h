// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "KnownAddressDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class InModuleDescriber : public Describer<Offset> {
 public:
  InModuleDescriber(const ProcessImage<Offset> *processImage,
                    const KnownAddressDescriber<Offset> &addressDescriber)
      : _knownAddressDescriber(addressDescriber) {
    SetProcessImage(processImage);
  }

  void SetProcessImage(const ProcessImage<Offset> *processImage) {
    if (processImage == 0) {
      _moduleDirectory = 0;
    } else {
      _moduleDirectory = &(processImage->GetModuleDirectory());
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
    std::string name;
    Offset base;
    Offset size;
    if (_moduleDirectory != 0 &&
        _moduleDirectory->Find(address, name, base, size)) {
      Commands::Output &output = context.GetOutput();
      output << "Address 0x" << std::hex << address << " is at offset 0x"
             << (address - base) << " in module " << name << " loaded at 0x"
             << base << ".\n";
      _knownAddressDescriber.Describe(context, address, explain);

      /*
       * This should also give some indication as to whether it is text
       * or data or something else.
       */
      if (explain) {
        /*
         * At some point this should explain why this area was identified
         * as belonging to a module.  This logic will be environment specific.
         */
      }
      return true;
    }
    return false;
  }

 protected:
   const ModuleDirectory<Offset> *_moduleDirectory;
   const KnownAddressDescriber<Offset>& _knownAddressDescriber;
};
}  // namespace chap
