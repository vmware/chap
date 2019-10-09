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
 * A TaggerRunner can tag all allocations in turn by allowing each tagger
 * multiple phases to tag starting from the given allocation.  An attempt
 * is made here to avoid the most expensive checks when possible and to
 * pick the best match when there is some minor ambiguity.
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
  typedef typename Tagger::Result Result;
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
    _resolved.reserve(_numTaggers);
    _resolved.resize(_numTaggers, false);
    RunOnePassThroughAllocations(Tagger::FIRST_PASS_THROUGH_ALLOCATIONS);
    RunOnePassThroughAllocations(Tagger::LAST_PASS_THROUGH_ALLOCATIONS);
  }

  void RunOnePassThroughAllocations(Pass pass) {
    Reader reader(_finder.GetAddressMap());
    for (AllocationIndex i = 0; i < _numAllocations; i++) {
      for (size_t i = 0; i < _numTaggers; ++i) {
        _resolved[i] = false;
      }
      _numResolved = 0;
      if (_tagHolder.GetTagIndex(i) != 0) {
        /*
         * This was already tagged, generally as a result of following
         * outgoing references from an allocation already being tagged.
         */
        continue;
      }
      const Allocation* allocation = _finder.AllocationAt(i);
      bool isUnsigned = true;
      if (allocation->Size() >= sizeof(Offset)) {
        Offset signatureCandidate =
            reader.ReadOffset(allocation->Address(), 0xbad);
        if (_signatureDirectory.IsMapped(signatureCandidate)) {
          isUnsigned = false;
        }
      }
      if (RunOnePhase(pass, i, Phase::QUICK_INITIAL_CHECK, *allocation,
                      isUnsigned) != Result::NOT_SURE_YET) {
        continue;
      }
      if (RunOnePhase(pass, i, Phase::MEDIUM_CHECK, *allocation, isUnsigned) !=
          Result::NOT_SURE_YET) {
        continue;
      }
      if (RunOnePhase(pass, i, Phase::SLOW_CHECK, *allocation, isUnsigned) !=
          Result::NOT_SURE_YET) {
        continue;
      }
      RunOnePhase(pass, i, Phase::WEAK_CHECK, *allocation, isUnsigned);
    }
  }

 private:
  const Finder& _finder;
  const AllocationIndex _numAllocations;
  const TagHolder& _tagHolder;
  const SignatureDirectory& _signatureDirectory;
  std::vector<Tagger*> _taggers;
  size_t _numTaggers;
  std::vector<bool> _resolved;
  size_t _numResolved;

  Result RunOnePhase(Pass pass, AllocationIndex index, Phase phase,
                     const Allocation& allocation, bool isUnsigned) {
    Result result;
    size_t resolvedIndex = 0;
    for (auto tagger : _taggers) {
      if (_resolved[resolvedIndex]) {
        continue;
      }
      result =
          tagger->TagFromAllocation(pass, index, phase, allocation, isUnsigned);
      if (result != Result::NOT_SURE_YET) {
        _resolved[resolvedIndex] = true;
        ++_numResolved;
        if (result == Result::TAGGING_DONE || _numResolved == _numTaggers) {
          return result;
        }
      }
      ++resolvedIndex;
    }
    return Result::NOT_SURE_YET;
  }
};
}  // namespace Allocations
}  // namespace chap
