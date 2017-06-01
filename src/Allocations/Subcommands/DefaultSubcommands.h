// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/SetBasedCommand.h"
#include "../../ProcessImage.h"
#include "../Iterators/Allocations.h"
#include "../Iterators/AnchorPoints.h"
#include "../Iterators/Anchored.h"
#include "../Iterators/Chain.h"
#include "../Iterators/ExactIncoming.h"
#include "../Iterators/ExternalAnchorPoints.h"
#include "../Iterators/ExternalAnchored.h"
#include "../Iterators/Free.h"
#include "../Iterators/Incoming.h"
#include "../Iterators/Leaked.h"
#include "../Iterators/Outgoing.h"
#include "../Iterators/FreeOutgoing.h"
#include "../Iterators/RegisterAnchorPoints.h"
#include "../Iterators/RegisterAnchored.h"
#include "../Iterators/ReverseChain.h"
#include "../Iterators/SingleAllocation.h"
#include "../Iterators/StackAnchorPoints.h"
#include "../Iterators/StackAnchored.h"
#include "../Iterators/StaticAnchorPoints.h"
#include "../Iterators/StaticAnchored.h"
#include "../Iterators/ThreadOnlyAnchorPoints.h"
#include "../Iterators/ThreadOnlyAnchored.h"
#include "../Iterators/Unreferenced.h"
#include "../Iterators/Used.h"
#include "SubcommandsForOneIterator.h"
namespace chap {
namespace Allocations {
namespace Subcommands {
template <class Offset>
class DefaultSubcommands {
 public:
  DefaultSubcommands()
      : _processImage(0),
        _singleAllocationSubcommands(_singleAllocationIteratorFactory),
        _allocationsSubcommands(_allocationsIteratorFactory),
        _usedSubcommands(_usedIteratorFactory),
        _freeSubcommands(_freeIteratorFactory),
        _leakedSubcommands(_leakedIteratorFactory),
        _unreferencedSubcommands(_unreferencedIteratorFactory),
        _anchoredSubcommands(_anchoredIteratorFactory),
        _anchorPointsSubcommands(_anchorPointsIteratorFactory),
        _staticAnchoredSubcommands(_staticAnchoredIteratorFactory),
        _staticAnchorPointsSubcommands(_staticAnchorPointsIteratorFactory),
        _stackAnchoredSubcommands(_stackAnchoredIteratorFactory),
        _stackAnchorPointsSubcommands(_stackAnchorPointsIteratorFactory),
        _registerAnchoredSubcommands(_registerAnchoredIteratorFactory),
        _registerAnchorPointsSubcommands(_registerAnchorPointsIteratorFactory),
        _externalAnchoredSubcommands(_externalAnchoredIteratorFactory),
        _externalAnchorPointsSubcommands(_externalAnchorPointsIteratorFactory),
        _threadOnlyAnchoredSubcommands(_threadOnlyAnchoredIteratorFactory),
        _threadOnlyAnchorPointsSubcommands(
            _threadOnlyAnchorPointsIteratorFactory),
        _incomingSubcommands(_incomingIteratorFactory),
        _exactIncomingSubcommands(_exactIncomingIteratorFactory),
        _outgoingSubcommands(_outgoingIteratorFactory),
        _freeOutgoingSubcommands(_freeOutgoingIteratorFactory),
        _chainSubcommands(_chainIteratorFactory),
        _reverseChainSubcommands(_reverseChainIteratorFactory) {}

  void SetProcessImage(const ProcessImage<Offset> *processImage) {
    _processImage = processImage;
    _singleAllocationSubcommands.SetProcessImage(processImage);
    _allocationsSubcommands.SetProcessImage(processImage);
    _usedSubcommands.SetProcessImage(processImage);
    _freeSubcommands.SetProcessImage(processImage);
    _leakedSubcommands.SetProcessImage(processImage);
    _unreferencedSubcommands.SetProcessImage(processImage);
    _anchoredSubcommands.SetProcessImage(processImage);
    _anchorPointsSubcommands.SetProcessImage(processImage);
    _staticAnchoredSubcommands.SetProcessImage(processImage);
    _staticAnchorPointsSubcommands.SetProcessImage(processImage);
    _stackAnchoredSubcommands.SetProcessImage(processImage);
    _stackAnchorPointsSubcommands.SetProcessImage(processImage);
    _registerAnchoredSubcommands.SetProcessImage(processImage);
    _registerAnchorPointsSubcommands.SetProcessImage(processImage);
    _externalAnchoredSubcommands.SetProcessImage(processImage);
    _externalAnchorPointsSubcommands.SetProcessImage(processImage);
    _threadOnlyAnchoredSubcommands.SetProcessImage(processImage);
    _threadOnlyAnchorPointsSubcommands.SetProcessImage(processImage);
    _incomingSubcommands.SetProcessImage(processImage);
    _exactIncomingSubcommands.SetProcessImage(processImage);
    _outgoingSubcommands.SetProcessImage(processImage);
    _freeOutgoingSubcommands.SetProcessImage(processImage);
    _chainSubcommands.SetProcessImage(processImage);
    _reverseChainSubcommands.SetProcessImage(processImage);
  }

  void RegisterSubcommands(Commands::Runner &runner) {
    _singleAllocationSubcommands.RegisterSubcommands(runner);
    _allocationsSubcommands.RegisterSubcommands(runner);
    _usedSubcommands.RegisterSubcommands(runner);
    _freeSubcommands.RegisterSubcommands(runner);
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
  }

 private:
  const ProcessImage<Offset> *_processImage;

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
};
}  // namespace Subcommands
}  // namespace Allocations
}  // namespace chap
