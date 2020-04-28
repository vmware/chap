// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../../SizedTally.h"
#include "../Directory.h"
#include "../SignatureDirectory.h"
namespace chap {
namespace Allocations {
namespace Visitors {
template <class Offset>
class Shower {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  class Factory {
   public:
    Factory() : _commandName("show") {}
    Shower* MakeVisitor(Commands::Context& context,
                        const ProcessImage<Offset>& processImage) {
      bool showAscii = false;
      (void)context.ParseBooleanSwitch("showAscii", showAscii);
      return new Shower(context, processImage.GetSignatureDirectory(),
                        processImage.GetVirtualAddressMap(), showAscii);
    }
    const std::string& GetCommandName() const { return _commandName; }
    // TODO: allow adding taints
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "In this case \"show\" means show the address, size,"
                " type if known, and contents of\n"
                "each allocation in the set.  For this process image,"
                " an allocation is shown as\nunsigned "
             << std::dec << (sizeof(Offset) * 8) << "-bit words.\n";
    }

   private:
    const std::string _commandName;
    const std::vector<std::string> _taints;
  };

  Shower(Commands::Context& context,
         const SignatureDirectory<Offset>& signatureDirectory,
         const VirtualAddressMap<Offset>& addressMap, bool showAscii)
      : _context(context),
        _signatureDirectory(signatureDirectory),
        _addressMap(addressMap),
        _showAscii(showAscii),
        _sizedTally(context, "allocations") {}
  void Visit(AllocationIndex /* index */, const Allocation& allocation) {
    size_t size = allocation.Size();
    _sizedTally.AdjustTally(size);
    Commands::Output& output = _context.GetOutput();

    if (allocation.IsUsed()) {
      output << "Used allocation at ";
    } else {
      output << "Free allocation at ";
    }
    Offset address = allocation.Address();
    output << std::hex << address << " of size " << size << "\n";
    const char* image;
    Offset numBytesFound = _addressMap.FindMappedMemoryImage(address, &image);
    if (numBytesFound < size) {
      // This is not expected to happen on Linux.
      output << "Note that allocation is not contiguously mapped.\n";
      size = numBytesFound;
    }

    if (size >= sizeof(Offset)) {
      Offset signature = *((Offset*)image);
      if (_signatureDirectory.IsMapped(signature)) {
        output << "... with signature " << signature;
        std::string name = _signatureDirectory.Name(signature);
        if (!name.empty()) {
          output << "(" << name << ")";
        }
        output << "\n";
      }
    }
    if (size > 0) {
      output.HexDump((const Offset*)image, size, _showAscii);
    }
    output << "\n";
  }

 private:
  Commands::Context& _context;
  const SignatureDirectory<Offset>& _signatureDirectory;
  const VirtualAddressMap<Offset>& _addressMap;
  const bool _showAscii;
  SizedTally<Offset> _sizedTally;
};
}  // namespace Visitors
}  // namespace Allocations
}  // namespace chap
