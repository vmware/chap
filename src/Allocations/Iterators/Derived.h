// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
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
class Derived {
 public:
  class Factory {
   public:
    Factory() : _setName("derived") {}
    Derived* MakeIterator(Commands::Context& /* context */,
                          const ProcessImage<Offset>& /* processImage */,
                          const Directory<Offset>& directory,
                          const SetCache<Offset>& setCache) {
      return new Derived(directory, directory.NumAllocations(),
                         setCache.GetDerived());
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 0; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "Use \"derived\" to specify the derived set.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;

  Derived(const Directory<Offset>& directory, AllocationIndex numAllocations,
          const Set<Offset>& derived)
      : _index(0),
        _directory(directory),
        _numAllocations(numAllocations),
        _derived(derived) {}
  AllocationIndex Next() {
    _index = _derived.NextUsed(_index);
    AllocationIndex next = _index;
    if (_index != _numAllocations) {
      ++_index;
    }
    return next;
  }

 private:
  AllocationIndex _index;
  const Directory<Offset>& _directory;
  AllocationIndex _numAllocations;
  const Set<Offset>& _derived;
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
