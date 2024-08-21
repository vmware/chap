// Copyright (c) 2017-2019 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "KnownAddressDescriber.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class InModuleDescriber : public Describer<Offset> {
 public:
  InModuleDescriber(const ProcessImage<Offset> &processImage,
                    const KnownAddressDescriber<Offset> &addressDescriber)
      : _knownAddressDescriber(addressDescriber),
        _moduleDirectory(processImage.GetModuleDirectory()) {}

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context &context, Offset address, bool explain,
                bool showAddresses) const {
    std::string name;
    Offset base;
    Offset size;
    Offset relativeVirtualAddress;
    if (_moduleDirectory.Find(address, name, base, size,
                              relativeVirtualAddress)) {
      Commands::Output &output = context.GetOutput();
      if (showAddresses) {
        output << "Address 0x" << std::hex << address << " is at offset 0x"
               << (address - base) << " in range\n[0x" << base << ", "
               << (base + size) << ")\nfor module " << name << "\n"
               << "and at module-relative virtual address 0x"
               << relativeVirtualAddress << ".\n";
        _knownAddressDescriber.Describe(context, address, explain, false);
      } else {
        // Note that we don't want to use the KnownAddressDescriber as currently
        // written for the range case because it shows information such as
        // permissions but we don't want this when we are displaying all the
        // ranges associated with certain permissions.
        output << "This is for module " << name << ".\n";
      }

      if (explain) {
        /*
         * At some point this should explain why this area was identified
         * as belonging to a module.  This logic will be environment specific.
         * It probably should show the file path and whether it is present
         * on the current host and whether the ELF headers match.
         */
      }
      return true;
    }
    return false;
  }

 protected:
  const KnownAddressDescriber<Offset> &_knownAddressDescriber;
  const ModuleDirectory<Offset> &_moduleDirectory;
};
}  // namespace chap
