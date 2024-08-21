// Copyright (c) 2017,2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Describer.h"
#include "../InModuleDescriber.h"
#include "../ProcessImage.h"
#include "../StackDescriber.h"
#include "AnchorChainLister.h"
#include "AnchorDirectory.h"
#include "Directory.h"
#include "PatternDescriberRegistry.h"
#include "SignatureDirectory.h"

namespace chap {
namespace Allocations {
template <typename Offset>
class Describer : public chap::Describer<Offset> {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  Describer(const InModuleDescriber<Offset>& inModuleDescriber,
            const StackDescriber<Offset>& stackDescriber,
            const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
            const ProcessImage<Offset>& processImage)
      : _inModuleDescriber(inModuleDescriber),
        _stackDescriber(stackDescriber),
        _patternDescriberRegistry(patternDescriberRegistry),
        _signatureDirectory(processImage.GetSignatureDirectory()),
        _anchorDirectory(processImage.GetAnchorDirectory()),
        _addressMap(processImage.GetVirtualAddressMap()),
        _directory(processImage.GetAllocationDirectory()),
        _graph(processImage.GetAllocationGraph()) {}

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context& context, Offset address, bool explain,
                bool showAddresses) const {
    if (_graph == 0) {
      return false;
    }
    AllocationIndex index = _directory.AllocationIndexOf(address);
    if (index == _directory.NumAllocations()) {
      return false;
    }
    const Allocation* allocation = _directory.AllocationAt(index);
    if (allocation == 0) {
      abort();
    }
    Describe(context, index, *allocation, explain,
             address - allocation->Address(), showAddresses);
    return true;
  }

  /*
   * Describe the range of memory that has the given page-aligned
   * address, but only if this describer covers the entire mapped range.
   */
  virtual bool DescribeRange(Commands::Context& /* context */,
                             Offset /* addressToDescribe */) const {
    // An allocation describer doesn't cover the entire range that
    // contains the allocation.
    return false;
  }

  void Describe(Commands::Context& context, AllocationIndex index,
                const Allocation& allocation, bool explain,
                Offset offsetInAllocation, bool showAddresses) const {
    size_t size = allocation.Size();
    Commands::Output& output = context.GetOutput();
    bool isUsed = false;
    bool isLeaked = false;
    bool isUnreferenced = false;
    bool isThreadCached = false;
    if (allocation.IsUsed()) {
      isUsed = true;
      if (_graph->IsLeaked(index)) {
        isLeaked = true;
        if (_graph->IsUnreferenced(index)) {
          isUnreferenced = true;
        }
      }
    } else {
      isThreadCached = _directory.IsThreadCached(index);
    }
    Offset address = allocation.Address();
    if (showAddresses) {
      output << "Address " << std::hex << (address + offsetInAllocation)
             << " is at offset " << offsetInAllocation << " of\n"
             << (!isUsed ? (isThreadCached ? "a thread-cached free" : "a free")
                         : !isLeaked ? "an anchored"
                                     : isUnreferenced ? "an unreferenced"
                                                      : "a leaked");
    } else {
      output << (!isUsed
                     ? (isThreadCached ? "Thread cached free" : "Free")
                     : !isLeaked ? "Anchored"
                                 : isUnreferenced ? "Unreferenced" : "Leaked");
    }
    output << " allocation at " << std::hex << address << " of size " << size
           << "\n";
    const char* image;
    (void)_addressMap.FindMappedMemoryImage(address, &image);
    bool isUnsigned = true;
    if (size >= sizeof(Offset)) {
      Offset signature = *((Offset*)image);
      if (_signatureDirectory.IsMapped(signature)) {
        isUnsigned = false;
        output << "... with signature " << signature;
        std::string name = _signatureDirectory.Name(signature);
        if (!name.empty()) {
          output << "(" << name << ")";
        }
        output << "\n";
      }
    }
    _patternDescriberRegistry.Describe(context, index, allocation, isUnsigned,
                                       explain);
    if (explain) {
      /*
       * We might at some point want to explain free allocations.  That
       * is very allocator specific.  In particular for free allocations
       * they might be thread cached (reserved for allocation by some
       * particular thread) or for libc malloc they might be on a fast
       * bin list or not.  An explanation of a free allocation might also
       * defer to the allocation finder.
       */
      if (isUsed) {
        if (!isLeaked) {
          AnchorChainLister<Offset> anchorChainLister(
              _inModuleDescriber, _stackDescriber, *_graph, _signatureDirectory,
              _anchorDirectory, context, address);
          _graph->VisitStaticAnchorChains(index, anchorChainLister);
          _graph->VisitRegisterAnchorChains(index, anchorChainLister);
          _graph->VisitStackAnchorChains(index, anchorChainLister);
        }
      }
    }
    output << "\n";
  }

 private:
  const InModuleDescriber<Offset>& _inModuleDescriber;
  const StackDescriber<Offset>& _stackDescriber;
  const PatternDescriberRegistry<Offset>& _patternDescriberRegistry;
  const SignatureDirectory<Offset>& _signatureDirectory;
  const AnchorDirectory<Offset>& _anchorDirectory;
  const VirtualAddressMap<Offset>& _addressMap;
  const Directory<Offset>& _directory;
  const Graph<Offset>* _graph;
};
}  // namespace Allocations
}  // namespace chap
