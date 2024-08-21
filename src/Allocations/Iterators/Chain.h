// Copyright (c) 2017,2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Directory.h"
#include "../SetCache.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class Chain {
 public:
  class Factory {
   public:
    Factory() : _setName("chain") {}
    Chain* MakeIterator(Commands::Context& context,
                        const ProcessImage<Offset>& processImage,
                        const Directory<Offset>& directory,
                        const SetCache<Offset>&) {
      Chain* iterator = 0;
      AllocationIndex numAllocations = directory.NumAllocations();
      size_t numPositionals = context.GetNumPositionals();
      Commands::Error& error = context.GetError();
      if (numPositionals < 4) {
        if (numPositionals < 3) {
          error << "No address was specified for a single allocation.\n";
        }
        error << "No offset was provided for the link field.\n";
      } else {
        Offset address;
        Offset linkOffset;
        if (!context.ParsePositional(2, address)) {
          error << context.Positional(2) << " is not a valid address.\n";
        } else if (!context.ParsePositional(3, linkOffset)) {
          error << context.Positional(3)
                << " is not a offset for the link field.\n";
        } else {
          AllocationIndex index = directory.AllocationIndexOf(address);
          if (index == numAllocations) {
            error << context.Positional(2)
                  << " is not part of an allocation.\n";
          } else {
            iterator = new Chain(directory, processImage.GetVirtualAddressMap(),
                                 index, numAllocations, linkOffset);
          }
        }
      }
      return iterator;
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 2; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "Use \"chain <address-in-hex> <offset-in-hex>\""
                " to specify a set starting at the\n"
                "allocation containing the specified address and following"
                " links at the given\n"
                "offset until the link offset doesn't fit in the allocation or"
                " the target is not\n"
                "in an allocation.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;

  Chain(const Directory<Offset>& directory,
        const VirtualAddressMap<Offset>& addressMap, AllocationIndex index,
        AllocationIndex numAllocations, Offset linkOffset)
      : _directory(directory),
        _addressMap(addressMap),
        _index(index),
        _numAllocations(numAllocations),
        _linkOffset(linkOffset) {}
  AllocationIndex Next() {
    AllocationIndex returnValue = _index;
    if (_index != _numAllocations) {
      const Allocation* allocation = _directory.AllocationAt(_index);
      if (allocation == 0) {
        abort();
      }
      _index = _numAllocations;
      Offset size = allocation->Size();
      Offset bytesNeeded = _linkOffset + sizeof(Offset);
      if (size >= bytesNeeded) {
        const char* image;
        Offset numBytesFound = _addressMap.FindMappedMemoryImage(
            allocation->Address() + _linkOffset, &image);
        if (numBytesFound >= sizeof(Offset)) {
          _index = _directory.AllocationIndexOf(*((Offset*)(image)));
        }
      }
    }
    return returnValue;
  }

 private:
  const Directory<Offset>& _directory;
  const VirtualAddressMap<Offset>& _addressMap;
  AllocationIndex _index;
  AllocationIndex _numAllocations;
  Offset _linkOffset;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
