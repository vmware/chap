// Copyright (c) 2025 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include "Annotator.h"

namespace chap {
template <typename Offset>
class ModuleAddressAnnotator : public Annotator<Offset> {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  ModuleAddressAnnotator(const ProcessImage<Offset>& processImage)
      : Annotator<Offset>("ModuleAddress"),
        _processImage(processImage),
        _moduleDirectory(processImage.GetModuleDirectory()) {}

  /*
   * Provide the actual annotation lines, excluding the information about the
   * address and the limit, with the specified prefix at the start of each line.
   */
  virtual Offset Annotate(
      Commands::Context& context,
      typename VirtualAddressMap<Offset>::Reader& reader,
      typename Annotator<Offset>::WriteHeaderFunction writeHeader,
      Offset address, Offset limit, const std::string& prefix) const {
    if (address + sizeof(Offset) > limit) {
      return address;
    }
    Offset inModuleAddress = reader.ReadOffset(address, 0);
    if (inModuleAddress == 0) {
      return address;
    }
    std::string name;
    Offset base;
    Offset size;
    Offset relativeVirtualAddress;
    if (!_moduleDirectory.Find(inModuleAddress, name, base, size,
                               relativeVirtualAddress)) {
      return address;
    }

    /*
     * Put the header for the annotation, showing the range covered and
     * the annotator name.
     */
    writeHeader(address, address + sizeof(Offset), Annotator<Offset>::_name);

    Commands::Output& output = context.GetOutput();
    output << prefix << "Address 0x" << std::hex << inModuleAddress
           << " is at offset 0x" << (inModuleAddress - base) << " in range\n"
           << prefix << "[0x" << base << ", " << (base + size) << ")\n"
           << prefix << "for module " << name << "\n"
           << prefix << "and at module-relative virtual address 0x"
           << relativeVirtualAddress << ".\n";

    return address + sizeof(Offset);
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const ModuleDirectory<Offset>& _moduleDirectory;
};
}  // namespace chap
