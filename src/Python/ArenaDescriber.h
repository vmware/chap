// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../VirtualAddressMap.h"
#include "InfrastructureFinder.h"

namespace chap {
namespace Python {
template <typename Offset>
class ArenaDescriber : public Describer<Offset> {
 public:
  ArenaDescriber(const InfrastructureFinder<Offset>& infrastructureFinder,
                 const VirtualAddressMap<Offset>& virtualAddressMap)
      : _infrastructureFinder(infrastructureFinder),
        _virtualAddressMap(virtualAddressMap),
        _arenaSize(infrastructureFinder.ArenaSize()),
        _poolSize(infrastructureFinder.PoolSize()),
        _arenaOffset(infrastructureFinder.ArenaOffset()),
        _poolsLimitOffset(infrastructureFinder.PoolsLimitOffset()) {}

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context& context, Offset address, bool explain,
                bool showAddresses) const {
    Offset arenaStruct = _infrastructureFinder.ArenaStructFor(address);
    if (arenaStruct != 0) {
      typename VirtualAddressMap<Offset>::Reader reader(_virtualAddressMap);
      Offset arena = reader.ReadOffset(arenaStruct + _arenaOffset);
      Offset poolsLimit = reader.ReadOffset(arenaStruct + _poolsLimitOffset);
      Offset firstPool = arena;
      if ((arena & (_poolSize - 1)) != 0) {
        firstPool = (arena + _poolSize - 1) & ~(_poolSize - 1);
      }
      Offset poolCandidate = address & ~(_poolSize - 1);
      Commands::Output& output = context.GetOutput();
      if (showAddresses) {
        output << "Address 0x" << std::hex << address << " is at offset 0x"
               << (address - arena) << " of a python arena at 0x" << arena
               << ".\n";
      } else {
        output << "This is in a python arena.\n";
      }
      if (explain) {
        if (address < firstPool) {
          output << "This is in an alignment region before the first pool "
                    " in the arena.\n";
        } else if (poolCandidate < poolsLimit) {
          output << "This is in a python pool at 0x" << std::hex
                 << poolCandidate << "\n";
        } else if ((poolCandidate + _poolSize) < (arena + _arenaSize)) {
          output << "This is in a region at the end of the arena available "
                    "for pool allocation.\n";
        } else {
          output << "This is in a trailing part of the arena too small to "
                    "contain a pool.\n";
        }
      }
      return true;
    }
    return false;
  }

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  const Offset _arenaSize;
  const Offset _poolSize;
  const Offset _arenaOffset;
  const Offset _poolsLimitOffset;
};

}  // namespace Python
}  // namespace chap
