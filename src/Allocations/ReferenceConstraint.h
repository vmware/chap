// Copyright (c) 2017,2020-2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <iostream>
#include <set>
#include <sstream>
#include "../VirtualAddressMap.h"
#include "Directory.h"
#include "EdgePredicate.h"
#include "Graph.h"
#include "PatternDescriberRegistry.h"
#include "SignatureChecker.h"
#include "TagHolder.h"

namespace chap {
namespace Allocations {
template <class Offset>
class ReferenceConstraint {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  typedef typename Graph<Offset>::EdgeIndex EdgeIndex;
  enum BoundaryType { MINIMUM, MAXIMUM };
  enum ReferenceType { INCOMING, OUTGOING };
  ReferenceConstraint(
      const SignatureDirectory<Offset>& signatureDirectory,
      const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
      const VirtualAddressMap<Offset>& addressMap, const std::string& signature,
      size_t count, bool wantUsed, BoundaryType boundaryType,
      ReferenceType referenceType, const Directory<Offset>& directory,
      const Graph<Offset>& graph, const TagHolder<Offset>& tagHolder,
      bool skipTaintedReferences, const EdgePredicate<Offset>& edgeIsTainted,
      bool skipUnfavoredReferences, const EdgePredicate<Offset>& edgeIsFavored)
      : _signatureChecker(signatureDirectory, patternDescriberRegistry,
                          addressMap, signature),
        _count(count),
        _wantUsed(wantUsed),
        _boundaryType(boundaryType),
        _referenceType(referenceType),
        _directory(directory),
        _graph(graph),
        _tagHolder(tagHolder),
        _skipTaintedReferences(skipTaintedReferences),
        _edgeIsTainted(edgeIsTainted),
        _skipUnfavoredReferences(skipUnfavoredReferences),
        _edgeIsFavored(edgeIsFavored) {}

  bool UnrecognizedSignature() const {
    return _signatureChecker.UnrecognizedSignature();
  }
  bool UnrecognizedPattern() const {
    return _signatureChecker.UnrecognizedPattern();
  }
  bool Check(AllocationIndex index) const {
    size_t numMatchingEdges = 0;
    if (_referenceType == INCOMING) {
      EdgeIndex firstIncoming;
      EdgeIndex pastIncoming;
      _graph.GetIncoming(index, firstIncoming, pastIncoming);
      bool skipUnfavoredReferences = _skipUnfavoredReferences;
      if (!_tagHolder.SupportsFavoredReferences(index)) {
        /*
         * If the target does not support favored references, there are no
         * unfavored references to skip.
         */
        skipUnfavoredReferences = false;
      }
      for (EdgeIndex nextIncoming = firstIncoming; nextIncoming != pastIncoming;
           nextIncoming++) {
        if (_skipTaintedReferences &&
            _edgeIsTainted.ForIncoming(nextIncoming)) {
          continue;
        }
        if (skipUnfavoredReferences &&
            !_edgeIsFavored.ForIncoming(nextIncoming)) {
          continue;
        }
        AllocationIndex sourceIndex = _graph.GetSourceForIncoming(nextIncoming);
        const Allocation& allocation = *(_directory.AllocationAt(sourceIndex));
        if ((allocation.IsUsed() == _wantUsed) &&
            (_signatureChecker.Check(sourceIndex, allocation))) {
          numMatchingEdges++;
        }
      }
    } else {
      EdgeIndex firstOutgoing;
      EdgeIndex pastOutgoing;
      _graph.GetOutgoing(index, firstOutgoing, pastOutgoing);
      for (EdgeIndex nextOutgoing = firstOutgoing; nextOutgoing != pastOutgoing;
           nextOutgoing++) {
        if (_skipTaintedReferences &&
            _edgeIsTainted.ForOutgoing(nextOutgoing)) {
          continue;
        }
        AllocationIndex targetIndex = _graph.GetTargetForOutgoing(nextOutgoing);
        if (_skipUnfavoredReferences &&
            _tagHolder.SupportsFavoredReferences(targetIndex) &&
            !_edgeIsFavored.ForOutgoing(nextOutgoing)) {
          continue;
        }
        const Allocation& allocation = *(_directory.AllocationAt(targetIndex));
        if ((allocation.IsUsed() == _wantUsed) &&
            (_signatureChecker.Check(targetIndex, allocation))) {
          numMatchingEdges++;
        }
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
  const TagHolder<Offset>& _tagHolder;
  bool _skipTaintedReferences;
  const EdgePredicate<Offset>& _edgeIsTainted;
  bool _skipUnfavoredReferences;
  const EdgePredicate<Offset>& _edgeIsFavored;
};
}  // namespace Allocations
}  // namespace chap
