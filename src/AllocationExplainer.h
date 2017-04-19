// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Allocations/Graph.h"
#include "AnchorChainLister.h"
#include "Explainer.h"
#include "InModuleExplainer.h"
#include "SignatureDirectory.h"
#include "StackExplainer.h"

namespace chap {
template <typename Offset>
class AllocationExplainer : public Explainer<Offset> {
 public:
  AllocationExplainer(const InModuleExplainer<Offset> &inModuleExplainer,
                      const StackExplainer<Offset> &stackExplainer,
                      const SignatureDirectory<Offset> *signatureDirectory)
      : _inModuleExplainer(inModuleExplainer),
        _stackExplainer(stackExplainer),
        _signatureDirectory(signatureDirectory) {}

  void SetAllocationGraph(const Allocations::Graph<Offset> *allocationGraph) {
    _graph = allocationGraph;
    if (_graph == NULL) {
      _finder = NULL;
    } else {
      _finder = &(_graph->GetAllocationFinder());
    }
  }

  void SetSignatureDirectory(const SignatureDirectory<Offset> *directory) {
    _signatureDirectory = directory;
  }

  /*
   * If the address is understood, provide an explanation for the address,
   * with output as specified and return true.  Otherwise don't write anything
   * and return false.
   */
  virtual bool Explain(Commands::Context &context,
                       Offset addressToExplain) const {
    if (_finder == NULL) {
      return false;
    }
    typename Allocations::Finder<Offset>::AllocationIndex index =
        _finder->AllocationIndexOf(addressToExplain);
    if (index == _finder->NumAllocations()) {
      return false;
    }
    const typename Allocations::Finder<Offset>::Allocation *allocation =
        _finder->AllocationAt(index);
    if (allocation == NULL) {
      context.GetError() << "Allocation index " << std::dec
                         << " appears to be invalid\n";
    } else {
      Offset start = allocation->Address();
      Offset size = allocation->Size();
      bool isAllocated = allocation->IsUsed();
      Commands::Output &output = context.GetOutput();
      output << "Address " << std::hex << addressToExplain << " is at offset 0x"
             << (addressToExplain - start)
             << (isAllocated ? " in a used allocation at "
                             : " in a free allocation at ")
             << start << " of size 0x" << size << "\n";
      if (isAllocated) {
        if (_graph->IsLeaked(index)) {
          output << "This allocation appears to be leaked.\n";
          if (_graph->IsUnreferenced(index)) {
            output << "This allocation appears to be unreferenced.\n";
          } else {
            // TODO: report what holds it.
          }
        } else {
          output << "This allocation appears to be anchored.\n";
          AnchorChainLister<Offset> anchorChainLister(
              _inModuleExplainer, _stackExplainer, *_graph, _signatureDirectory,
              context, start);
          _graph->VisitStaticAnchorChains(index, anchorChainLister);
          _graph->VisitRegisterAnchorChains(index, anchorChainLister);
          _graph->VisitStackAnchorChains(index, anchorChainLister);
        }
      }
      return true;
    }
    return false;
  }

 private:
  const Allocations::Graph<Offset> *_graph;
  const Allocations::Finder<Offset> *_finder;
  const InModuleExplainer<Offset> &_inModuleExplainer;
  const StackExplainer<Offset> &_stackExplainer;
  const SignatureDirectory<Offset> *_signatureDirectory;
};
}  // namespace chap
