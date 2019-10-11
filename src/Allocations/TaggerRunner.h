// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Finder.h"
#include "SignatureDirectory.h"
#include "TagHolder.h"
#include "Tagger.h"

namespace chap {
namespace Allocations {

/*
 * A TaggerRunner does two passes through all allocations.  On each pass,
 * for each allocation, each tagger is given multiple opportunities to
 * examine the allocation, with the goal of possibly tagging that allocation
 * and/or possibly tagging allocations reached from that allocation by following
 * references.  An attempt here is to avoid the most expensive checks when
 * possible and to pick the best match when there is some minor ambiguity.
 */
template <typename Offset>
class TaggerRunner {
 public:
  typedef Finder<Offset> Finder;
  typedef typename Finder::AllocationIndex AllocationIndex;
  typedef typename Finder::Allocation Allocation;
  typedef Tagger<Offset> Tagger;
  typedef typename Tagger::Pass Pass;
  typedef typename Tagger::Phase Phase;
  typedef TagHolder<Offset> TagHolder;
  typedef SignatureDirectory<Offset> SignatureDirectory;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  TaggerRunner(const Finder& finder, const TagHolder& tagHolder,
               const SignatureDirectory& signatureDirectory)
      : _finder(finder),
        _numAllocations(finder.NumAllocations()),
        _tagHolder(tagHolder),
        _signatureDirectory(signatureDirectory) {}

  void RegisterTagger(Tagger* t) { _taggers.push_back(t); }
  void ResolveAllAllocationTags() {
    _numTaggers = _taggers.size();
    _finishedWithPass.reserve(_numTaggers);
    _finishedWithPass.resize(_numTaggers, false);
    RunOnePassThroughAllocations(Tagger::FIRST_PASS_THROUGH_ALLOCATIONS);
    RunOnePassThroughAllocations(Tagger::LAST_PASS_THROUGH_ALLOCATIONS);
  }

  void RunOnePassThroughAllocations(Pass pass) {
    Reader reader(_finder.GetAddressMap());
    for (AllocationIndex i = 0; i < _numAllocations; i++) {
      for (size_t i = 0; i < _numTaggers; ++i) {
        _finishedWithPass[i] = false;
      }
      _numFinishedWithPass = 0;
      const Allocation* allocation = _finder.AllocationAt(i);
      bool isUnsigned = true;
      if (allocation->Size() >= sizeof(Offset)) {
        Offset signatureCandidate =
            reader.ReadOffset(allocation->Address(), 0xbad);
        if (_signatureDirectory.IsMapped(signatureCandidate)) {
          isUnsigned = false;
        }
      }
      if (!RunOnePhase(reader, pass, i, Phase::QUICK_INITIAL_CHECK, *allocation,
                       isUnsigned) &&
          !RunOnePhase(reader, pass, i, Phase::MEDIUM_CHECK, *allocation,
                       isUnsigned) &&
          !RunOnePhase(reader, pass, i, Phase::SLOW_CHECK, *allocation,
                       isUnsigned)) {
        RunOnePhase(reader, pass, i, Phase::WEAK_CHECK, *allocation,
                    isUnsigned);
      }
    }
  }

 private:
  const Finder& _finder;
  const AllocationIndex _numAllocations;
  const TagHolder& _tagHolder;
  const SignatureDirectory& _signatureDirectory;
  std::vector<Tagger*> _taggers;
  size_t _numTaggers;
  std::vector<bool> _finishedWithPass;
  size_t _numFinishedWithPass;

  bool RunOnePhase(Reader& reader, Pass pass, AllocationIndex index,
                   Phase phase, const Allocation& allocation, bool isUnsigned) {
    size_t resolvedIndex = 0;
    for (auto tagger : _taggers) {
      if (_finishedWithPass[resolvedIndex]) {
        continue;
      }
      if (tagger->TagFromAllocation(reader, pass, index, phase, allocation,
                                    isUnsigned)) {
        _finishedWithPass[resolvedIndex] = true;
        if (++_numFinishedWithPass == _numTaggers) {
          return true;
        }
      }
      ++resolvedIndex;
    }
    return false;
  }
};
}  // namespace Allocations
}  // namespace chap
