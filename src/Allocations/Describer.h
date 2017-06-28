// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Describer.h"
#include "../InModuleDescriber.h"
#include "../ProcessImage.h"
#include "../SignatureDirectory.h"
#include "../StackDescriber.h"
#include "../AnchorChainLister.h"
#include "Finder.h"

namespace chap {
namespace Allocations {
template <typename Offset>
class Describer : public chap::Describer<Offset> {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  Describer(const InModuleDescriber<Offset>& inModuleDescriber,
            const StackDescriber<Offset>& stackDescriber,
            const ProcessImage<Offset>* processImage)
      : _inModuleDescriber(inModuleDescriber), _stackDescriber(stackDescriber) {
    SetProcessImage(processImage);
  }

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    if (processImage == 0) {
      _signatureDirectory = 0;
      _addressMap = 0;
      _finder = 0;
      _graph = 0;
    } else {
      _signatureDirectory = &(processImage->GetSignatureDirectory());
      _addressMap = &(processImage->GetVirtualAddressMap());
      _finder = processImage->GetAllocationFinder();
      _graph = processImage->GetAllocationGraph();
    }
  }

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.
   */
  bool Describe(Commands::Context& context, Offset address,
                bool explain) const {
    if (_processImage == 0 || _finder == 0 || _graph == 0) {
      return false;
    }
    AllocationIndex index = _finder->AllocationIndexOf(address);
    if (index == _finder->NumAllocations()) {
      return false;
    }
    const Allocation* allocation = _finder->AllocationAt(index);
    if (allocation == 0) {
      abort();
    }
    Describe(context, index, *allocation, explain);
    return true;
  }

  void Describe(Commands::Context& context, AllocationIndex index,
                const Allocation& allocation, bool explain) const {
    size_t size = allocation.Size();
    Commands::Output& output = context.GetOutput();
    bool isUsed = false;
    bool isLeaked = false;
    bool isUnreferenced = false;
    if (allocation.IsUsed()) {
      isUsed = true;
      if (_graph->IsLeaked(index)) {
        isLeaked = true;
        if (_graph->IsUnreferenced(index)) {
          isUnreferenced = true;
        }
      }
    }
    output << (!isUsed ? "Free"
                       : !isLeaked ? "Anchored"
                                   : isUnreferenced ? "Unreferenced" : "Leaked")
           << " allocation at ";
    Offset address = allocation.Address();
    output << std::hex << address << " of size " << size << "\n";
    const char* image;
    (void)_addressMap->FindMappedMemoryImage(address, &image);
    if (size >= sizeof(Offset)) {
      Offset signature = *((Offset*)image);
      if (_signatureDirectory->IsMapped(signature)) {
        output << "... with signature " << signature;
        std::string name = _signatureDirectory->Name(signature);
        if (!name.empty()) {
          output << "(" << name << ")";
        }
        output << "\n";
      }
    }
    if (explain) {
      /*
       * We might at some point want to explain free allocations.  That
       * is very allocator specific.
       */
      if (isUsed) {
        if (!isLeaked) {
          AnchorChainLister<Offset> anchorChainLister(
              _inModuleDescriber, _stackDescriber,
              *_graph, _signatureDirectory,
              context, address);
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
  const ProcessImage<Offset>* _processImage;
  const SignatureDirectory<Offset>* _signatureDirectory;
  const VirtualAddressMap<Offset>* _addressMap;
  const Finder<Offset>* _finder;
  const Graph<Offset>* _graph;
};
}  // namespace Allocations
}  // namespace chap
