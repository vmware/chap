// Copyright (c) 2018-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../../SizedTally.h"
#include "../InfrastructureFinder.h"
namespace chap {
namespace LibcMalloc {
namespace Subcommands {
template <class Offset>
class DescribeArenas : public Commands::Subcommand {
 public:
  DescribeArenas(const InfrastructureFinder<Offset>& infrastructureFinder,
                 const Allocations::Directory<Offset>& directory)
      : Commands::Subcommand("describe", "arenas"),
        _infrastructureFinder(infrastructureFinder),
        _arenas(infrastructureFinder.GetArenas()),
        _directory(directory) {
    SetArenaTallies();
  }

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This subcommand describes all the arenas associated "
           "with libc malloc.\n";
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, "arenas");
    for (const auto& addressAndInfo : _arenas) {
      Offset address = addressAndInfo.first;
      const typename InfrastructureFinder<Offset>::Arena arena =
          addressAndInfo.second;
      tally.AdjustTally(arena._maxSize);
      ArenaTally& arenaTally = _arenaTallies[address];
      output << "Arena at 0x" << std::hex << address << " has size 0x"
             << arena._size << " (" << std::dec << arena._size
             << ")\nand maximum size 0x" << std::hex << arena._maxSize << " ("
             << std::dec << arena._maxSize << ").\n"
             << std::dec << arenaTally._freeCount << " free allocations take 0x"
             << std::hex << arenaTally._freeBytes << " (" << std::dec
             << arenaTally._freeBytes << ") bytes.\n"
             << std::dec << arenaTally._usedCount << " used allocations take 0x"
             << std::hex << arenaTally._usedBytes << " (" << std::dec
             << arenaTally._usedBytes << ") bytes.\n\n";
    }
  }

 private:
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const typename InfrastructureFinder<Offset>::ArenaMap& _arenas;
  const Allocations::Directory<Offset>& _directory;
  struct ArenaTally {
    ArenaTally() : _freeCount(0), _freeBytes(0), _usedCount(0), _usedBytes(0) {}
    Offset _freeCount;
    Offset _freeBytes;
    Offset _usedCount;
    Offset _usedBytes;
  };
  std::map<Offset, ArenaTally> _arenaTallies;
  void SetArenaTallies() {
    typename Allocations::Directory<Offset>::AllocationIndex numAllocations =
        _directory.NumAllocations();
    for (typename Allocations::Directory<Offset>::AllocationIndex i = 0;
         i < numAllocations; ++i) {
      const typename Allocations::Directory<Offset>::Allocation* allocation =
          _directory.AllocationAt(i);
      Offset arenaAddress =
          _infrastructureFinder.ArenaAddressFor(allocation->Address());
      if (arenaAddress != 0) {
        ArenaTally& arenaTally = _arenaTallies[arenaAddress];
        if (allocation->IsUsed()) {
          arenaTally._usedCount++;
          arenaTally._usedBytes += allocation->Size();
        } else {
          arenaTally._freeCount++;
          arenaTally._freeBytes += allocation->Size();
        }
      }
    }
  }
};
}  // namespace Subcommands
}  // namespace LibcMalloc
}  // namespace chap
