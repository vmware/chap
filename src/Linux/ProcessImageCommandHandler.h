// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ProcessImageCommandHandler.h"
#include "COWStringBodyRecognizer.h"
#include "LibcMallocHeapDescriber.h"
#include "LibcMallocMainArenaRunDescriber.h"
#include "LibcMallocMmappedAllocationDescriber.h"
#include "LinuxProcessImage.h"
#include "LongStringRecognizer.h"
#include "PyDictKeysObjectRecognizer.h"
#include "SSLRecognizer.h"
#include "SSL_CTXRecognizer.h"
#include "Subcommands/DescribeArenas.h"

namespace chap {
namespace Linux {
template <typename ElfImage>
class ProcessImageCommandHandler
    : public chap::ProcessImageCommandHandler<typename ElfImage::Offset> {
 public:
  typedef typename ElfImage::Offset Offset;
  typedef typename chap::ProcessImageCommandHandler<Offset> Base;
  ProcessImageCommandHandler(const LinuxProcessImage<ElfImage> &processImage)
      : Base(processImage),
        _describeArenasSubcommand(processImage.GetLibcMallocAllocationFinder()),
        _libcMallocHeapDescriber(processImage.GetLibcMallocAllocationFinder(),
                                 processImage.GetVirtualAddressMap()),
        _libcMallocMainArenaRunDescriber(
            processImage.GetLibcMallocAllocationFinder()),
        _libcMallocMmappedAllocationDescriber(
            processImage.GetLibcMallocAllocationFinder()),
        _COWStringBodyRecognizer(processImage),
        _longStringRecognizer(processImage),
        _SSL_CTXRecognizer(processImage),
        _SSLRecognizer(processImage),
        _pyDictKeysObjectRecognizer(processImage) {
    Base::_patternRecognizerRegistry.Register(_COWStringBodyRecognizer);
    Base::_patternRecognizerRegistry.Register(_longStringRecognizer);
    Base::_patternRecognizerRegistry.Register(_SSL_CTXRecognizer);
    Base::_patternRecognizerRegistry.Register(_SSLRecognizer);
    Base::_patternRecognizerRegistry.Register(_pyDictKeysObjectRecognizer);
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
    Base::_compoundDescriber.AddDescriber(_libcMallocHeapDescriber);
    Base::_compoundDescriber.AddDescriber(_libcMallocMainArenaRunDescriber);
    Base::_compoundDescriber.AddDescriber(
        _libcMallocMmappedAllocationDescriber);

    Base::_compoundDescriber.AddDescriber(Base::_stackOverflowGuardDescriber);
    /*
     * The following should alway be added last because describers are
     * checked in the order given and the first applicable describer applies.
     */
    Base::_compoundDescriber.AddDescriber(Base::_knownAddressDescriber);
  }

  virtual void AddCommands(Commands::Runner &r) {
    Base::AddCommands(r);
    Base::RegisterSubcommand(r, _describeArenasSubcommand);
  }

 private:
  Subcommands::DescribeArenas<Offset> _describeArenasSubcommand;
  LibcMallocHeapDescriber<Offset> _libcMallocHeapDescriber;
  LibcMallocMainArenaRunDescriber<Offset> _libcMallocMainArenaRunDescriber;
  LibcMallocMmappedAllocationDescriber<Offset>
      _libcMallocMmappedAllocationDescriber;
  COWStringBodyRecognizer<Offset> _COWStringBodyRecognizer;
  LongStringRecognizer<Offset> _longStringRecognizer;
  SSL_CTXRecognizer<Offset> _SSL_CTXRecognizer;
  SSLRecognizer<Offset> _SSLRecognizer;
  PyDictKeysObjectRecognizer<Offset> _pyDictKeysObjectRecognizer;
};

}  // namespace Linux
}  // namespace chap
