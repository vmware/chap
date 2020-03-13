// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Directory.h"
namespace chap {
namespace Allocations {
namespace Iterators {
template <class Offset>
class ThreadCached {
 public:
  class Factory {
   public:
    Factory() : _setName("threadcached") {}
    ThreadCached* MakeIterator(Commands::Context& /* context */,
                               const ProcessImage<Offset>& /* processImage */,
                               const Directory<Offset>& directory) {
      return new ThreadCached(directory, directory.NumAllocations());
    }
    // TODO: allow adding taints
    const std::string& GetSetName() const { return _setName; }
    size_t GetNumArguments() { return 0; }
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "Use \"threadcached\" to specify"
                " the set of all free allocations in per-thread\ncache.\n";
    }

   private:
    const std::vector<std::string> _taints;
    const std::string _setName;
  };
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;

  ThreadCached(const Directory<Offset>& directory,
               AllocationIndex numAllocations)
      : _index(directory.HasThreadCached() ? 0 : numAllocations),
        _directory(directory),
        _numAllocations(numAllocations) {}
  AllocationIndex Next() {
    while (_index != _numAllocations && !_directory.IsThreadCached(_index)) {
      ++_index;
    }
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
};
}  // namespace Iterators
}  // namespace Allocations
}  // namespace chap
