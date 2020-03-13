// Copyright (c) 2017,2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <iostream>
#include <set>
#include <sstream>
#include "../VirtualAddressMap.h"
#include "Directory.h"
#include "Graph.h"
#include "PatternDescriberRegistry.h"
#include "SignatureChecker.h"

namespace chap {
namespace Allocations {
template <class Offset>
class ReferenceConstraint {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  enum BoundaryType { MINIMUM, MAXIMUM };
  enum ReferenceType { INCOMING, OUTGOING };
  ReferenceConstraint(
      const SignatureDirectory<Offset>& signatureDirectory,
      const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
      const VirtualAddressMap<Offset>& addressMap, const std::string& signature,
      size_t count, bool wantUsed, BoundaryType boundaryType,
      ReferenceType referenceType, const Directory<Offset>& directory,
      const Graph<Offset>& graph)

      : _signatureChecker(signatureDirectory, patternDescriberRegistry,
                          addressMap, signature),
        _count(count),
        _wantUsed(wantUsed),
        _boundaryType(boundaryType),
        _referenceType(referenceType),
        _directory(directory),
        _graph(graph) {}
  bool UnrecognizedSignature() const {
    return _signatureChecker.UnrecognizedSignature();
  }
  bool UnrecognizedPattern() const {
    return _signatureChecker.UnrecognizedPattern();
  }
  bool Check(AllocationIndex index) const {
    size_t numMatchingEdges = 0;
    const AllocationIndex* pFirstEdge;
    const AllocationIndex* pPastEdge;
    if (_referenceType == INCOMING) {
      _graph.GetIncoming(index, &pFirstEdge, &pPastEdge);
    } else {
      _graph.GetOutgoing(index, &pFirstEdge, &pPastEdge);
    }
    for (const AllocationIndex* pEdge = pFirstEdge; pEdge != pPastEdge;
         pEdge++) {
      const Allocation& allocation = *(_directory.AllocationAt(*pEdge));
      if ((allocation.IsUsed() == _wantUsed) &&
          (_signatureChecker.Check(*pEdge, allocation))) {
        numMatchingEdges++;
      }
    }
    return (_boundaryType == MINIMUM) ? (numMatchingEdges >= _count)
                                      : (numMatchingEdges <= _count);
  }

 private:
  SignatureChecker<Offset> _signatureChecker;
  size_t _count;
  bool _wantUsed;
  BoundaryType _boundaryType;
  ReferenceType _referenceType;
  const Directory<Offset>& _directory;
  const Graph<Offset>& _graph;
};
}  // namespace Allocations
}  // namespace chap
