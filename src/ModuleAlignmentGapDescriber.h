// Copyright (c) 2019 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class ModuleAlignmentGapDescriber : public Describer<Offset> {
  typedef typename VirtualMemoryPartition<Offset>::ClaimedRanges ClaimedRanges;

 public:
  ModuleAlignmentGapDescriber(const ProcessImage<Offset> &processImage)
      : _moduleDirectory(processImage.GetModuleDirectory()),
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
    if (it->_value != _moduleDirectory.MODULE_ALIGNMENT_GAP) {
      return false;
    }
    Offset gapBase = it->_base;
    Offset gapLimit = it->_limit;
    std::string name;
    Offset base;
    Offset size;
    Offset relativeVirtualAddress;
    if (!_moduleDirectory.Find(gapBase - 1, name, base, size,
                               relativeVirtualAddress)) {
      return false;
    }
    Commands::Output &output = context.GetOutput();
    if (showAddresses) {
      output << "Address 0x" << std::hex << address << " is at offset 0x"
             << (address - gapBase) << " in module alignment gap\n[0x"
             << gapBase << ", 0x" << gapLimit << ")\nfor module " << name
             << ".\n";
    } else {
      output << "This alignment gap is for module " << name << ".\n";
    }

    if (explain) {
      if (foundAsReadOnly) {
        output << "The gap is marked readable, likely due to a bug in "
                  "creation of the core.\n";
      } else {
        if (_virtualAddressMap.find(address) == _virtualAddressMap.end()) {
          output << "The gap is not listed in the core but is inferred "
                    "based on the adjacent ranges.\n";
        }
      }
    }
    return true;
  }

 private:
  const ModuleDirectory<Offset> &_moduleDirectory;
  const VirtualAddressMap<Offset> &_virtualAddressMap;
  const VirtualMemoryPartition<Offset> &_virtualMemoryPartition;
  const ClaimedRanges &_inaccessibleRanges;
  const ClaimedRanges &_readOnlyRanges;
};
}  // namespace chap
