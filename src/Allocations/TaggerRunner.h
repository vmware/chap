// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "ContiguousImage.h"
#include "Finder.h"
#include "Graph.h"
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
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  typedef typename Tagger<Offset>::Phase Phase;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  TaggerRunner(const Graph<Offset>& graph, const TagHolder<Offset>& tagHolder,
               const SignatureDirectory<Offset>& signatureDirectory)
      : _graph(graph),
        _finder(graph.GetAllocationFinder()),
        _contiguousImage(_finder),
        _numAllocations(_finder.NumAllocations()),
        _tagHolder(tagHolder),
        _signatureDirectory(signatureDirectory) {}

  void RegisterTagger(Tagger<Offset>* t) { _taggers.push_back(t); }
  void ResolveAllAllocationTags() {
    _numTaggers = _taggers.size();
    _finishedWithPass.reserve(_numTaggers);
    _finishedWithPass.resize(_numTaggers, false);
    TagFromAllocations();
    TagFromReferenced();
  }

 private:
  const Graph<Offset>& _graph;
  const Finder<Offset>& _finder;
  ContiguousImage<Offset> _contiguousImage;
  const AllocationIndex _numAllocations;
  const TagHolder<Offset>& _tagHolder;
  const SignatureDirectory<Offset>& _signatureDirectory;
  std::vector<Tagger<Offset>*> _taggers;
  size_t _numTaggers;
  std::vector<bool> _finishedWithPass;
  size_t _numFinishedWithPass;

  /*
   * For each used allocation, attempt to tag it and any referenced
   * allocations for which the tag is implied directly as a result
   * of the newly added tag.
   */

  void TagFromAllocations() {
    Reader reader(_finder.GetAddressMap());
    for (AllocationIndex i = 0; i < _numAllocations; i++) {
      const Allocation* allocation = _finder.AllocationAt(i);
      if (!allocation->IsUsed()) {
        continue;
      }
      _contiguousImage.SetIndex(i);
      for (size_t taggersIndex = 0; taggersIndex < _numTaggers;
           ++taggersIndex) {
        _finishedWithPass[taggersIndex] = false;
      }
      _numFinishedWithPass = 0;
      bool isUnsigned = true;
      if (allocation->Size() >= sizeof(Offset)) {
        Offset signatureCandidate =
            reader.ReadOffset(allocation->Address(), 0xbad);
        if (_signatureDirectory.IsMapped(signatureCandidate)) {
          isUnsigned = false;
        }
      }
      if (!RunTagFromAllocationPhase(reader, i, Phase::QUICK_INITIAL_CHECK,
                                     *allocation, isUnsigned) &&
          !RunTagFromAllocationPhase(reader, i, Phase::MEDIUM_CHECK,
                                     *allocation, isUnsigned) &&
          !RunTagFromAllocationPhase(reader, i, Phase::SLOW_CHECK, *allocation,
                                     isUnsigned)) {
        RunTagFromAllocationPhase(reader, i, Phase::WEAK_CHECK, *allocation,
                                  isUnsigned);
      }
    }
  }

  /*
   * For each used allocation, regardless of whether it has already been
   * tagged, use the contents of that allocation to attempt to tag any
   * allocations referenced by it that have not yet been tagged.
   */

  void TagFromReferenced() {
    Reader reader(_finder.GetAddressMap());
    std::vector<AllocationIndex> unresolvedOutgoing;
    unresolvedOutgoing.reserve(_finder.MaxAllocationSize());
    for (AllocationIndex i = 0; i < _numAllocations; i++) {
      const Allocation* allocation = _finder.AllocationAt(i);
      if (!allocation->IsUsed()) {
        continue;
      }
      _contiguousImage.SetIndex(i);
      unresolvedOutgoing.clear();
      size_t numUnresolved = 0;
      const Offset* offsetLimit = _contiguousImage.OffsetLimit();
      for (const Offset* check = _contiguousImage.FirstOffset();
           check < offsetLimit; check++) {
        AllocationIndex targetIndex = _graph.TargetAllocationIndex(i, *check);
        if (targetIndex != _numAllocations) {
          if (_tagHolder.GetTagIndex(targetIndex) != 0) {
            targetIndex = _numAllocations;
          } else {
            numUnresolved++;
          }
        }
        unresolvedOutgoing.push_back(targetIndex);
      }
      if (numUnresolved == 0) {
        continue;
      }
      for (size_t taggersIndex = 0; taggersIndex < _numTaggers;
           ++taggersIndex) {
        _finishedWithPass[taggersIndex] = false;
      }
      _numFinishedWithPass = 0;
      bool isUnsigned = true;
      if (allocation->Size() >= sizeof(Offset)) {
        Offset signatureCandidate =
            reader.ReadOffset(allocation->Address(), 0xbad);
        if (_signatureDirectory.IsMapped(signatureCandidate)) {
          isUnsigned = false;
        }
      }
      AllocationIndex* pUnresolvedOutgoing = &(unresolvedOutgoing[0]);
      if (!RunTagFromReferencedPhase(reader, i, Phase::QUICK_INITIAL_CHECK,
                                     *allocation, pUnresolvedOutgoing) &&
          !RunTagFromReferencedPhase(reader, i, Phase::MEDIUM_CHECK,
                                     *allocation, pUnresolvedOutgoing) &&
          !RunTagFromReferencedPhase(reader, i, Phase::SLOW_CHECK, *allocation,
                                     pUnresolvedOutgoing)) {
        RunTagFromReferencedPhase(reader, i, Phase::WEAK_CHECK, *allocation,
                                  pUnresolvedOutgoing);
      }
    }
  }

  bool RunTagFromAllocationPhase(Reader& reader, AllocationIndex index,
                                 Phase phase, const Allocation& allocation,
                                 bool isUnsigned) {
    size_t resolvedIndex = 0;
    for (auto tagger : _taggers) {
      if (_finishedWithPass[resolvedIndex]) {
        ++resolvedIndex;
        continue;
      }
      if (tagger->TagFromAllocation(_contiguousImage, reader, index, phase,
                                    allocation, isUnsigned)) {
        _finishedWithPass[resolvedIndex] = true;
        if (++_numFinishedWithPass == _numTaggers) {
          return true;
        }
      }
      ++resolvedIndex;
    }
    return false;
  }

  bool RunTagFromReferencedPhase(Reader& reader, AllocationIndex index,
                                 Phase phase, const Allocation& allocation,
                                 AllocationIndex* unresolvedOutgoing) {
    size_t resolvedIndex = 0;
    for (auto tagger : _taggers) {
      if (_finishedWithPass[resolvedIndex]) {
        ++resolvedIndex;
        continue;
      }
      if (tagger->TagFromReferenced(_contiguousImage, reader, index, phase,
                                    allocation, unresolvedOutgoing)) {
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
