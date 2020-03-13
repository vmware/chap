// Copyright (c) 2017-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../LibcMalloc/FinderGroup.h"
#include "../LibcMalloc/Subcommands/DescribeArenas.h"
#include "../ProcessImageCommandHandler.h"
#include "LinuxProcessImage.h"

namespace chap {
namespace Linux {
template <typename ElfImage>
class ProcessImageCommandHandler
    : public chap::ProcessImageCommandHandler<typename ElfImage::Offset> {
 public:
  typedef typename ElfImage::Offset Offset;
  typedef typename chap::ProcessImageCommandHandler<Offset> Base;
  ProcessImageCommandHandler(const LinuxProcessImage<ElfImage>& processImage)
      : Base(processImage),
        _libcMallocFinderGroup(processImage.GetLibcMallocFinderGroup()),
        _describeArenasSubcommand(
            _libcMallocFinderGroup.GetInfrastructureFinder(),
            processImage.GetAllocationDirectory()) {
    Base::_compoundDescriber.AddDescriber(Base::_allocationDescriber);
    Base::_compoundDescriber.AddDescriber(Base::_stackDescriber);

    /*
     * Any describers for statically allocated structures should for now be
     * added
     * before the _inModuleDescriber, so that they can have priority.
     */
    // TODO: provide main arena describer here.
    /*
     * This one needs to be before the _inModuleDescriber because it is more
     * specific and so should be tried first.
     */
    Base::_compoundDescriber.AddDescriber(Base::_moduleAlignmentGapDescriber);
    Base::_compoundDescriber.AddDescriber(Base::_inModuleDescriber);
    /*
     * Describers specific to libc malloc should be done rather late, both
     * because
     * it will be rare that they are the best describer used for a given address
     * and specifically because we prefer allocations to be described as such,
     * as
     * opposed to being described in some allocator-specific way.  So at a
     * minimum
     * they should never be added before the _allocationDescriber.
     */
    _libcMallocFinderGroup.AddDescribers(Base::_compoundDescriber);

    Base::_compoundDescriber.AddDescriber(Base::_stackOverflowGuardDescriber);
    Base::_compoundDescriber.AddDescriber(Base::_pythonArenaDescriber);
    /*
     * The following should alway be added last because describers are
     * checked in the order given and the first applicable describer applies.
     */
    Base::_compoundDescriber.AddDescriber(Base::_knownAddressDescriber);
  }

  virtual void AddCommands(Commands::Runner& r) {
    Base::AddCommands(r);
    Base::RegisterSubcommand(r, _describeArenasSubcommand);
  }

 private:
  LibcMalloc::FinderGroup<Offset>& _libcMallocFinderGroup;
  LibcMalloc::Subcommands::DescribeArenas<Offset> _describeArenasSubcommand;
};

}  // namespace Linux
}  // namespace chap
