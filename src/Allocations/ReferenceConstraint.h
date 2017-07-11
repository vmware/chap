// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <iostream>
#include <set>
#include <sstream>
#include "../VirtualAddressMap.h"
#include "Finder.h"
#include "Graph.h"
#include "SignatureChecker.h"

namespace chap {
namespace Allocations {
template <class Offset>
class ReferenceConstraint {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  enum BoundaryType { MINIMUM, MAXIMUM };
  enum ReferenceType { INCOMING, OUTGOING };
  ReferenceConstraint(const SignatureDirectory<Offset>& directory,
                      const VirtualAddressMap<Offset>& addressMap,
                      const std::string& signature, size_t count,
                      BoundaryType boundaryType, ReferenceType referenceType,
                      const Finder<Offset>& finder, const Graph<Offset>& graph)

      : _signatureChecker(directory, addressMap, signature),
        _count(count),
        _boundaryType(boundaryType),
        _referenceType(referenceType),
        _finder(finder),
        _graph(graph) {}
  bool UnrecognizedSignature() const {
    return _signatureChecker.UnrecognizedSignature();
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
      if (_signatureChecker.Check(*(_finder.AllocationAt(*pEdge)))) {
        numMatchingEdges++;
      }
    }
    return (_boundaryType == MINIMUM) ? (numMatchingEdges >= _count)
                                      : (numMatchingEdges <= _count);
  }

 private:
  SignatureChecker<Offset> _signatureChecker;
  size_t _count;
  BoundaryType _boundaryType;
  ReferenceType _referenceType;
  const Finder<Offset>& _finder;
  const Graph<Offset>& _graph;
};
}  // namespace Allocations
}  // namespace chap
