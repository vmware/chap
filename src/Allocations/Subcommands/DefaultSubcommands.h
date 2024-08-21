// Copyright (c) 2017-2019,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../AnnotatorRegistry.h"
#include "../../Commands/Runner.h"
#include "../../Commands/SetBasedCommand.h"
#include "../../ProcessImage.h"
#include "../Describer.h"
#include "../Iterators/Allocations.h"
#include "../Iterators/AnchorPoints.h"
#include "../Iterators/Anchored.h"
#include "../Iterators/Chain.h"
#include "../Iterators/Derived.h"
#include "../Iterators/ExactIncoming.h"
#include "../Iterators/ExternalAnchorPoints.h"
#include "../Iterators/ExternalAnchored.h"
#include "../Iterators/Free.h"
#include "../Iterators/FreeOutgoing.h"
#include "../Iterators/Incoming.h"
#include "../Iterators/Leaked.h"
#include "../Iterators/Outgoing.h"
#include "../Iterators/RegisterAnchorPoints.h"
#include "../Iterators/RegisterAnchored.h"
#include "../Iterators/ReverseChain.h"
#include "../Iterators/SingleAllocation.h"
#include "../Iterators/StackAnchorPoints.h"
#include "../Iterators/StackAnchored.h"
#include "../Iterators/StaticAnchorPoints.h"
#include "../Iterators/StaticAnchored.h"
#include "../Iterators/ThreadCached.h"
#include "../Iterators/ThreadOnlyAnchorPoints.h"
#include "../Iterators/ThreadOnlyAnchored.h"
#include "../Iterators/Unreferenced.h"
#include "../Iterators/Used.h"
#include "../PatternDescriberRegistry.h"
#include "../SetCache.h"
#include "../Visitors/DefaultVisitorFactories.h"
#include "SubcommandsForOneIterator.h"
namespace chap {
namespace Allocations {
namespace Subcommands {
template <class Offset>
class DefaultSubcommands {
 public:
  DefaultSubcommands(
      const ProcessImage<Offset> &processImage,
      const Describer<Offset> &describer,
      const PatternDescriberRegistry<Offset> &patternDescriberRegistry,
      const AnnotatorRegistry<Offset> &annotatorRegistry)
      : _defaultVisitorFactories(describer),
        _setCache(processImage.GetAllocationDirectory().NumAllocations()),
        _singleAllocationSubcommands(
            processImage, _singleAllocationIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _allocationsSubcommands(
            processImage, _allocationsIteratorFactory, _defaultVisitorFactories,
            patternDescriberRegistry, annotatorRegistry, _setCache),
        _usedSubcommands(processImage, _usedIteratorFactory,
                         _defaultVisitorFactories, patternDescriberRegistry,
                         annotatorRegistry, _setCache),
        _freeSubcommands(processImage, _freeIteratorFactory,
                         _defaultVisitorFactories, patternDescriberRegistry,
                         annotatorRegistry, _setCache),
        _threadCachedSubcommands(processImage, _threadCachedIteratorFactory,
                                 _defaultVisitorFactories,
                                 patternDescriberRegistry, annotatorRegistry,
                                 _setCache),
        _leakedSubcommands(processImage, _leakedIteratorFactory,
                           _defaultVisitorFactories, patternDescriberRegistry,
                           annotatorRegistry, _setCache),
        _unreferencedSubcommands(processImage, _unreferencedIteratorFactory,
                                 _defaultVisitorFactories,
                                 patternDescriberRegistry, annotatorRegistry,
                                 _setCache),
        _anchoredSubcommands(processImage, _anchoredIteratorFactory,
                             _defaultVisitorFactories, patternDescriberRegistry,
                             annotatorRegistry, _setCache),
        _anchorPointsSubcommands(processImage, _anchorPointsIteratorFactory,
                                 _defaultVisitorFactories,
                                 patternDescriberRegistry, annotatorRegistry,
                                 _setCache),
        _staticAnchoredSubcommands(processImage, _staticAnchoredIteratorFactory,
                                   _defaultVisitorFactories,
                                   patternDescriberRegistry, annotatorRegistry,
                                   _setCache),
        _staticAnchorPointsSubcommands(
            processImage, _staticAnchorPointsIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _stackAnchoredSubcommands(processImage, _stackAnchoredIteratorFactory,
                                  _defaultVisitorFactories,
                                  patternDescriberRegistry, annotatorRegistry,
                                  _setCache),
        _stackAnchorPointsSubcommands(
            processImage, _stackAnchorPointsIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _registerAnchoredSubcommands(
            processImage, _registerAnchoredIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _registerAnchorPointsSubcommands(
            processImage, _registerAnchorPointsIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _externalAnchoredSubcommands(
            processImage, _externalAnchoredIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _externalAnchorPointsSubcommands(
            processImage, _externalAnchorPointsIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _threadOnlyAnchoredSubcommands(
            processImage, _threadOnlyAnchoredIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _threadOnlyAnchorPointsSubcommands(
            processImage, _threadOnlyAnchorPointsIteratorFactory,
            _defaultVisitorFactories, patternDescriberRegistry,
            annotatorRegistry, _setCache),
        _incomingSubcommands(processImage, _incomingIteratorFactory,
                             _defaultVisitorFactories, patternDescriberRegistry,
                             annotatorRegistry, _setCache),
        _exactIncomingSubcommands(processImage, _exactIncomingIteratorFactory,
                                  _defaultVisitorFactories,
                                  patternDescriberRegistry, annotatorRegistry,
                                  _setCache),
        _outgoingSubcommands(processImage, _outgoingIteratorFactory,
                             _defaultVisitorFactories, patternDescriberRegistry,
                             annotatorRegistry, _setCache),
        _freeOutgoingSubcommands(processImage, _freeOutgoingIteratorFactory,
                                 _defaultVisitorFactories,
                                 patternDescriberRegistry, annotatorRegistry,
                                 _setCache),
        _chainSubcommands(processImage, _chainIteratorFactory,
                          _defaultVisitorFactories, patternDescriberRegistry,
                          annotatorRegistry, _setCache),
        _reverseChainSubcommands(processImage, _reverseChainIteratorFactory,
                                 _defaultVisitorFactories,
                                 patternDescriberRegistry, annotatorRegistry,
                                 _setCache),
        _derivedSubcommands(processImage, _derivedIteratorFactory,
                            _defaultVisitorFactories, patternDescriberRegistry,
                            annotatorRegistry, _setCache) {}

  void RegisterSubcommands(Commands::Runner &runner) {
    _singleAllocationSubcommands.RegisterSubcommands(runner);
    _allocationsSubcommands.RegisterSubcommands(runner);
    _usedSubcommands.RegisterSubcommands(runner);
    _freeSubcommands.RegisterSubcommands(runner);
    _threadCachedSubcommands.RegisterSubcommands(runner);
    _leakedSubcommands.RegisterSubcommands(runner);
    _unreferencedSubcommands.RegisterSubcommands(runner);
    _anchoredSubcommands.RegisterSubcommands(runner);
    _anchorPointsSubcommands.RegisterSubcommands(runner);
    _staticAnchoredSubcommands.RegisterSubcommands(runner);
    _staticAnchorPointsSubcommands.RegisterSubcommands(runner);
    _stackAnchoredSubcommands.RegisterSubcommands(runner);
    _stackAnchorPointsSubcommands.RegisterSubcommands(runner);
    _registerAnchoredSubcommands.RegisterSubcommands(runner);
    _registerAnchorPointsSubcommands.RegisterSubcommands(runner);
    _externalAnchoredSubcommands.RegisterSubcommands(runner);
    _externalAnchorPointsSubcommands.RegisterSubcommands(runner);
    _threadOnlyAnchoredSubcommands.RegisterSubcommands(runner);
    _threadOnlyAnchorPointsSubcommands.RegisterSubcommands(runner);
    _incomingSubcommands.RegisterSubcommands(runner);
    _exactIncomingSubcommands.RegisterSubcommands(runner);
    _outgoingSubcommands.RegisterSubcommands(runner);
    _freeOutgoingSubcommands.RegisterSubcommands(runner);
    _chainSubcommands.RegisterSubcommands(runner);
    _reverseChainSubcommands.RegisterSubcommands(runner);
    _derivedSubcommands.RegisterSubcommands(runner);
  }

 private:
  typename Visitors::DefaultVisitorFactories<Offset> _defaultVisitorFactories;
  SetCache<Offset> _setCache;

  typedef typename Iterators::SingleAllocation<Offset> SingleAllocationIterator;
  typename SingleAllocationIterator::Factory _singleAllocationIteratorFactory;
  SubcommandsForOneIterator<Offset, SingleAllocationIterator>
      _singleAllocationSubcommands;

  typedef typename Iterators::Allocations<Offset> AllocationsIterator;
  typename AllocationsIterator::Factory _allocationsIteratorFactory;
  SubcommandsForOneIterator<Offset, AllocationsIterator>
      _allocationsSubcommands;

  typedef typename Iterators::Used<Offset> UsedIterator;
  typename UsedIterator::Factory _usedIteratorFactory;
  SubcommandsForOneIterator<Offset, UsedIterator> _usedSubcommands;

  typedef typename Iterators::Free<Offset> FreeIterator;
  typename FreeIterator::Factory _freeIteratorFactory;
  SubcommandsForOneIterator<Offset, FreeIterator> _freeSubcommands;

  typedef typename Iterators::ThreadCached<Offset> ThreadCachedIterator;
  typename ThreadCachedIterator::Factory _threadCachedIteratorFactory;
  SubcommandsForOneIterator<Offset, ThreadCachedIterator>
      _threadCachedSubcommands;

  typedef typename Iterators::Leaked<Offset> LeakedIterator;
  typename LeakedIterator::Factory _leakedIteratorFactory;
  SubcommandsForOneIterator<Offset, LeakedIterator> _leakedSubcommands;

  typedef typename Iterators::Unreferenced<Offset> UnreferencedIterator;
  typename UnreferencedIterator::Factory _unreferencedIteratorFactory;
  SubcommandsForOneIterator<Offset, UnreferencedIterator>
      _unreferencedSubcommands;

  typedef typename Iterators::Anchored<Offset> AnchoredIterator;
  typename AnchoredIterator::Factory _anchoredIteratorFactory;
  SubcommandsForOneIterator<Offset, AnchoredIterator> _anchoredSubcommands;

  typedef typename Iterators::AnchorPoints<Offset> AnchorPointsIterator;
  typename AnchorPointsIterator::Factory _anchorPointsIteratorFactory;
  SubcommandsForOneIterator<Offset, AnchorPointsIterator>
      _anchorPointsSubcommands;

  typedef typename Iterators::StaticAnchored<Offset> StaticAnchoredIterator;
  typename StaticAnchoredIterator::Factory _staticAnchoredIteratorFactory;
  SubcommandsForOneIterator<Offset, StaticAnchoredIterator>
      _staticAnchoredSubcommands;

  typedef typename Iterators::StaticAnchorPoints<Offset>
      StaticAnchorPointsIterator;
  typename StaticAnchorPointsIterator::Factory
      _staticAnchorPointsIteratorFactory;
  SubcommandsForOneIterator<Offset, StaticAnchorPointsIterator>
      _staticAnchorPointsSubcommands;

  typedef typename Iterators::StackAnchored<Offset> StackAnchoredIterator;
  typename StackAnchoredIterator::Factory _stackAnchoredIteratorFactory;
  SubcommandsForOneIterator<Offset, StackAnchoredIterator>
      _stackAnchoredSubcommands;

  typedef typename Iterators::StackAnchorPoints<Offset>
      StackAnchorPointsIterator;
  typename StackAnchorPointsIterator::Factory _stackAnchorPointsIteratorFactory;
  SubcommandsForOneIterator<Offset, StackAnchorPointsIterator>
      _stackAnchorPointsSubcommands;

  typedef typename Iterators::RegisterAnchored<Offset> RegisterAnchoredIterator;
  typename RegisterAnchoredIterator::Factory _registerAnchoredIteratorFactory;
  SubcommandsForOneIterator<Offset, RegisterAnchoredIterator>
      _registerAnchoredSubcommands;

  typedef typename Iterators::RegisterAnchorPoints<Offset>
      RegisterAnchorPointsIterator;
  typename RegisterAnchorPointsIterator::Factory
      _registerAnchorPointsIteratorFactory;
  SubcommandsForOneIterator<Offset, RegisterAnchorPointsIterator>
      _registerAnchorPointsSubcommands;

  typedef typename Iterators::ExternalAnchored<Offset> ExternalAnchoredIterator;
  typename ExternalAnchoredIterator::Factory _externalAnchoredIteratorFactory;
  SubcommandsForOneIterator<Offset, ExternalAnchoredIterator>
      _externalAnchoredSubcommands;

  typedef typename Iterators::ExternalAnchorPoints<Offset>
      ExternalAnchorPointsIterator;
  typename ExternalAnchorPointsIterator::Factory
      _externalAnchorPointsIteratorFactory;
  SubcommandsForOneIterator<Offset, ExternalAnchorPointsIterator>
      _externalAnchorPointsSubcommands;

  typedef typename Iterators::ThreadOnlyAnchored<Offset>
      ThreadOnlyAnchoredIterator;
  typename ThreadOnlyAnchoredIterator::Factory
      _threadOnlyAnchoredIteratorFactory;
  SubcommandsForOneIterator<Offset, ThreadOnlyAnchoredIterator>
      _threadOnlyAnchoredSubcommands;

  typedef typename Iterators::ThreadOnlyAnchorPoints<Offset>
      ThreadOnlyAnchorPointsIterator;
  typename ThreadOnlyAnchorPointsIterator::Factory
      _threadOnlyAnchorPointsIteratorFactory;
  SubcommandsForOneIterator<Offset, ThreadOnlyAnchorPointsIterator>
      _threadOnlyAnchorPointsSubcommands;

  typedef typename Iterators::Incoming<Offset> IncomingIterator;
  typename IncomingIterator::Factory _incomingIteratorFactory;
  SubcommandsForOneIterator<Offset, IncomingIterator> _incomingSubcommands;

  typedef typename Iterators::ExactIncoming<Offset> ExactIncomingIterator;
  typename ExactIncomingIterator::Factory _exactIncomingIteratorFactory;
  SubcommandsForOneIterator<Offset, ExactIncomingIterator>
      _exactIncomingSubcommands;

  typedef typename Iterators::Outgoing<Offset> OutgoingIterator;
  typename OutgoingIterator::Factory _outgoingIteratorFactory;
  SubcommandsForOneIterator<Offset, OutgoingIterator> _outgoingSubcommands;

  typedef typename Iterators::FreeOutgoing<Offset> FreeOutgoingIterator;
  typename FreeOutgoingIterator::Factory _freeOutgoingIteratorFactory;
  SubcommandsForOneIterator<Offset, FreeOutgoingIterator>
      _freeOutgoingSubcommands;

  typedef typename Iterators::Chain<Offset> ChainIterator;
  typename ChainIterator::Factory _chainIteratorFactory;
  SubcommandsForOneIterator<Offset, ChainIterator> _chainSubcommands;

  typedef typename Iterators::ReverseChain<Offset> ReverseChainIterator;
  typename ReverseChainIterator::Factory _reverseChainIteratorFactory;
  SubcommandsForOneIterator<Offset, ReverseChainIterator>
      _reverseChainSubcommands;

  typedef typename Iterators::Derived<Offset> DerivedIterator;
  typename DerivedIterator::Factory _derivedIteratorFactory;
  SubcommandsForOneIterator<Offset, DerivedIterator> _derivedSubcommands;
};
}  // namespace Subcommands
}  // namespace Allocations
}  // namespace chap
