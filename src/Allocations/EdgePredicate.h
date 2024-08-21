// Copyright (c) 2021 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <vector>
#include "Graph.h"

namespace chap {
namespace Allocations {
template <class Offset>
class EdgePredicate {
 public:
  typedef typename Directory<Offset>::AllocationIndex Index;
  typedef typename Graph<Offset>::EdgeIndex EdgeIndex;
  EdgePredicate(const Graph<Offset>& graph, bool defaultValue)
      : _graph(graph), _totalEdges(graph.TotalEdges()) {
    _valueByOutgoingEdgeIndex.reserve((size_t)_totalEdges);
    _valueByOutgoingEdgeIndex.resize((size_t)_totalEdges, defaultValue);
    _valueByIncomingEdgeIndex.reserve((size_t)_totalEdges);
    _valueByIncomingEdgeIndex.resize((size_t)_totalEdges, defaultValue);
  }

  void SetAllOutgoing(Index source, bool value) {
    EdgeIndex firstOutgoing, pastOutgoing;
    _graph.GetOutgoing(source, firstOutgoing, pastOutgoing);
    for (EdgeIndex outgoing = firstOutgoing; outgoing != pastOutgoing;
         outgoing++) {
      EdgeIndex incoming = _graph.GetIncomingEdgeIndex(
          source, _graph.GetTargetForOutgoing(outgoing));
      _valueByOutgoingEdgeIndex[outgoing] = value;
      _valueByIncomingEdgeIndex[incoming] = value;
    }
  }

  void SetAllIncoming(Index target, bool value) {
    EdgeIndex firstIncoming, pastIncoming;
    _graph.GetIncoming(target, firstIncoming, pastIncoming);
    for (EdgeIndex incoming = firstIncoming; incoming != pastIncoming;
         incoming++) {
      EdgeIndex outgoing = _graph.GetOutgoingEdgeIndex(
          _graph.GetSourceForIncoming(incoming), target);
      _valueByIncomingEdgeIndex[incoming] = value;
      _valueByOutgoingEdgeIndex[outgoing] = value;
    }
  }

  void Set(Index source, Index target, bool value) {
    EdgeIndex incoming = _graph.GetIncomingEdgeIndex(source, target);
    if (incoming == _totalEdges) {
      return;
    }
    _valueByIncomingEdgeIndex[incoming] = value;
    _valueByOutgoingEdgeIndex[_graph.GetOutgoingEdgeIndex(source, target)] =
        value;
  }

  bool For(Index source, Index target) const {
    EdgeIndex incoming = _graph.GetIncomingEdgeIndex(source, target);
    return (incoming == _totalEdges) ? false
                                     : _valueByIncomingEdgeIndex[incoming];
  }

  bool ForIncoming(EdgeIndex index) const {
    return (index < _totalEdges) && (_valueByIncomingEdgeIndex[index] == true);
  }

  bool ForOutgoing(EdgeIndex index) const {
    return (index < _totalEdges) && (_valueByOutgoingEdgeIndex[index] == true);
  }

 private:
  const Graph<Offset>& _graph;
  const EdgeIndex _totalEdges;
  std::vector<bool> _valueByOutgoingEdgeIndex;
  std::vector<bool> _valueByIncomingEdgeIndex;
};
}  // namespace Allocations
}  // namespace chap
