// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <regex>
#include <stack>
#include "../Commands/Runner.h"
#include "../ProcessImage.h"
#include "Finder.h"
#include "Graph.h"
#include "SignatureChecker.h"
#include "SignatureDirectory.h"

/*
 * This keeps mappings from signature to name and name to set of signatures.
 * Note that there are potentially multiple signatures (numbers) for a given
 * name because a signature may be defined in multiple load modules.
 */

namespace chap {
namespace Allocations {
template <class Offset, class Visitor>
class ExtendedVisitor {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  ExtendedVisitor(Commands::Context& context,
                  const ProcessImage<Offset>& processImage)
      : _isEnabled(false),
        _hasErrors(false),
        _graph(0),
        _finder(processImage.GetAllocationFinder()),
        _addressMap(processImage.GetVirtualAddressMap()),
        _numAllocations(_finder->NumAllocations()) {
    size_t numExtensionArguments = context.GetNumArguments("extend");
    if (numExtensionArguments == 0) {
      return;
    }
    Commands::Error& error = context.GetError();
    //  [signature-or-label][@offset-in-member]<direction
    //  indicator>[signature][@offset-in-signature][:stateLabel]
    std::regex extensionRegex(
        "([^@]*)(@([[:xdigit:]]+))?"   // specify the starting members
        "((->)|(<-))"                  // extend by outgoing or incoming refs
        "([^@=]*)(@([[:xdigit:]]+))?"  // constrain the type of extension
        "(=>(\\w+))?");                // select a new extension state
    std::smatch extensionSmatch;
    std::map<std::string, size_t> labelToStateNumber;
    labelToStateNumber[""] = 0;
    std::vector<Specification> specifications;
    specifications.reserve(numExtensionArguments);
    for (size_t i = 0; i < numExtensionArguments; i++) {
      const std::string extensionRule = context.Argument("extend", i);
      if (!std::regex_match(extensionRule, extensionSmatch, extensionRegex)) {
        error << "Extension specification \"" << extensionRule
              << "\" is ill formed.\n";
        _hasErrors = true;
        continue;
      }
      specifications.emplace_back();
      Specification& spec = specifications.back();
      spec._memberSignature = extensionSmatch[1];
      if (extensionSmatch[3].length() > 0) {
        spec._useOffsetInMember = true;
        size_t offsetInMember;
        std::istringstream is(extensionSmatch[3]);
        is >> std::hex >> offsetInMember;
        if (!is.fail() && is.eof()) {
          spec._offsetInMember = offsetInMember;
        } else {
          error << "Offset in member \"" << extensionSmatch[3]
                << " is not well formed as hexadecimal.\n";
          _hasErrors = true;
        }
      }
      spec._referenceIsOutgoing = (extensionSmatch[5].length() > 0);
      spec._extensionSignature = extensionSmatch[7];
      if (extensionSmatch[9].length() > 0) {
        spec._useOffsetInExtension = true;
        size_t offsetInExtension;
        std::istringstream is(extensionSmatch[9]);
        is >> std::hex >> offsetInExtension;
        if (!is.fail() && is.eof()) {
          spec._offsetInExtension = offsetInExtension;
        } else {
          error << "Offset in extension \"" << extensionSmatch[9]
                << " is not well formed as hexadecimal.\n";
          _hasErrors = true;
        }
      }

      std::string stateLabel = extensionSmatch[11];
      size_t stateIndex = 0;
      std::map<std::string, size_t>::iterator it =
          labelToStateNumber.find(stateLabel);
      if (it != labelToStateNumber.end()) {
        stateIndex = it->second;
      } else {
        stateIndex = labelToStateNumber.size();
        labelToStateNumber[stateLabel] = stateIndex;
      }
      spec._newState = stateIndex;
    }

    /*
     * Now that all the state names are known, identify any cases where a
     * state label was provided instead of a member signature.  Don't bother
     * with specifications that were already rejected as ill formed.
     */

    size_t numStates = labelToStateNumber.size();
    _stateToBase.resize(numStates + 1, 0);
    for (typename std::vector<Specification>::iterator it =
             specifications.begin();
         it != specifications.end(); ++it) {
      Specification& spec = *it;
      if (!spec._memberSignature.empty()) {
        std::map<std::string, size_t>::const_iterator itState =
            labelToStateNumber.find(spec._memberSignature);
        if (itState != labelToStateNumber.end()) {
          spec._baseState = itState->second;
          spec._memberSignature = "";
          _stateToBase[spec._baseState]++;
          continue;
        }
      }
      _stateToBase[0]++;
    }

    // Convert contents of _stateToBase from counts to limits.
    for (size_t i = 1; i <= numStates; ++i) {
      _stateToBase[i] += _stateToBase[i - 1];
    }

    /*
     * Map from rule index to argument index (so the rules are in an
     * efficient order to process) and convert the contents of _stateToBase
     * from limits to bases.
     */

    size_t numSpecs = specifications.size();
    std::vector<size_t> ruleIndexToArgumentIndex;
    ruleIndexToArgumentIndex.resize(numSpecs);
    for (size_t i = numSpecs; i-- > 0;) {
      ruleIndexToArgumentIndex[--(_stateToBase[specifications[i]._baseState])] =
          i;
    }

    const SignatureDirectory<Offset>& signatureDirectory =
        processImage.GetSignatureDirectory();

    /*
     * Create the extension rules in the calculated order.
     */

    _rules.reserve(numSpecs);
    for (size_t i = 0; i < numSpecs; i++) {
      _rules.emplace_back(signatureDirectory, _addressMap,
                          specifications[ruleIndexToArgumentIndex[i]]);
      Rule& rule = _rules.back();
      if (rule._memberSignatureChecker.UnrecognizedSignature()) {
        error << "Member signature \""
              << rule._memberSignatureChecker.GetSignature()
              << "\" is not recognized.\n";
        _hasErrors = true;
      }
      if (rule._extensionSignatureChecker.UnrecognizedSignature()) {
        error << "Extension signature \""
              << rule._extensionSignatureChecker.GetSignature()
              << "\" is not recognized.\n";
        _hasErrors = true;
      }
    }
    if (!_hasErrors) {
      if (_rules[0]._baseState != 0) {
        /*
         * If all of the rules apply to some extension state other than the
         * base state, no extensions will be done because it would require
         * at least one extension from the base state to leave it.  It might
         * also be valid to let the command just run (and leave extensions
         * disabled to avoid doing needless checks on each object in the
         * original
         * set) but probably the user would prefer to correct the command and
         * not to wait for a command with broken extension rules to complete
         * first.
         */
        error << "None of the extension rules can be applied to the "
                 "set to be extended.\n";
        _hasErrors = true;
      } else {
        _isEnabled = true;
        _graph = processImage.GetAllocationGraph();
        _hasErrors = (_graph == 0);
        _visited.resize(_numAllocations, false);
      }
    }
  }

  bool IsEnabled() const { return _isEnabled; }
  bool HasErrors() { return _hasErrors; }

 private:
  enum RuleCheckProgress { NEW_RULE, NO_EDGES_CHECKED, IN_PROGRESS, RULE_DONE };
  struct ExtensionContext {
    ExtensionContext(AllocationIndex memberIndex, size_t ruleIndex,
                     size_t numCandidatesLeft,
                     RuleCheckProgress ruleCheckProgress,
                     const AllocationIndex* pNextCandidate)
        : _memberIndex(memberIndex),
          _ruleIndex(ruleIndex),
          _numCandidatesLeft(numCandidatesLeft),
          _ruleCheckProgress(ruleCheckProgress),
          _pNextCandidate(pNextCandidate) {}
    AllocationIndex _memberIndex;
    size_t _ruleIndex;
    size_t _numCandidatesLeft;
    RuleCheckProgress _ruleCheckProgress;
    const AllocationIndex* _pNextCandidate;
  };

 public:
  void Visit(AllocationIndex memberIndex, const Allocation& allocation,
             Visitor& visitor) {
    /*
     * If the extended visitor is disabled, just visit members of the set.
     */
    if (!_isEnabled) {
      visitor.Visit(memberIndex, allocation);
      return;
    }

    /*
     * If the extended visitor is enabled, but we already visited the given
     * set member as an extension to the set, don't visit it again.
     */

    if (_visited[memberIndex]) {
      return;
    }

    /*
     * Visit the given member of the set before looking for any extensions.
     */

    _visited[memberIndex] = true;
    visitor.Visit(memberIndex, allocation);

    std::stack<ExtensionContext> extensionContexts;
    size_t state = 0;
    size_t ruleIndex = _stateToBase[state];
    size_t numCandidatesLeft = 0;
    size_t ruleIndexLimit = _stateToBase[state + 1];
    const Allocation* memberAllocation = &allocation;
    const AllocationIndex* pNextCandidate = 0;
    const AllocationIndex* pPastCandidates = 0;
    RuleCheckProgress ruleCheckProgress = RuleCheckProgress::NEW_RULE;

    while (true) {
      if (ruleCheckProgress == RuleCheckProgress::RULE_DONE) {
        if (++ruleIndex == ruleIndexLimit) {
          if (extensionContexts.empty()) {
            return;
          }
          ExtensionContext& extensionContext = extensionContexts.top();
          memberIndex = extensionContext._memberIndex;
          ruleIndex = extensionContext._ruleIndex;
          numCandidatesLeft = extensionContext._numCandidatesLeft;
          ruleCheckProgress = extensionContext._ruleCheckProgress;
          pNextCandidate = extensionContext._pNextCandidate;
          extensionContexts.pop();

          memberAllocation = _finder->AllocationAt(memberIndex);
          state = _rules[ruleIndex]._baseState;
          ruleIndexLimit = _stateToBase[state + 1];
          continue;
        } else {
          ruleCheckProgress = RuleCheckProgress::NEW_RULE;
        }
      }
      Rule& rule = _rules[ruleIndex];
      AllocationIndex candidateIndex = _numAllocations;
      const Allocation* candidateAllocation = 0;
      if (ruleCheckProgress == RuleCheckProgress::NEW_RULE) {
        if (!rule._memberSignatureChecker.Check(*memberAllocation) ||
            (rule._useOffsetInMember &&
             (rule._offsetInMember +
                  (rule._referenceIsOutgoing ? sizeof(Offset) : 1) >
              memberAllocation->Size()))) {
          ruleCheckProgress = RuleCheckProgress::RULE_DONE;
          continue;
        }
        if (rule._referenceIsOutgoing) {
          if (rule._useOffsetInMember) {
            ruleCheckProgress = RuleCheckProgress::RULE_DONE;
            const char* image;
            Offset numBytesFound = _addressMap.FindMappedMemoryImage(
                memberAllocation->Address() + rule._offsetInMember, &image);
            if (numBytesFound < sizeof(Offset)) {
              continue;
            }
            Offset target = *((Offset*)(image));
            candidateIndex = _finder->AllocationIndexOf(target);
            if (candidateIndex == _numAllocations) {
              continue;
            }
            candidateAllocation = _finder->AllocationAt(candidateIndex);
            if (rule._useOffsetInExtension &&
                target != (candidateAllocation->Address() +
                           rule._offsetInExtension)) {
              continue;
            }
          } else {
            _graph->GetOutgoing(memberIndex, &pNextCandidate, &pPastCandidates);
            ruleCheckProgress = RuleCheckProgress::NO_EDGES_CHECKED;
          }
        } else {
          _graph->GetIncoming(memberIndex, &pNextCandidate, &pPastCandidates);
          ruleCheckProgress = RuleCheckProgress::NO_EDGES_CHECKED;
        }
      }
      if (ruleCheckProgress == RuleCheckProgress::NO_EDGES_CHECKED) {
        numCandidatesLeft = pPastCandidates - pNextCandidate;
        if (numCandidatesLeft == 0) {
          ruleCheckProgress = RuleCheckProgress::RULE_DONE;
          continue;
        }
        ruleCheckProgress = IN_PROGRESS;
      }

      if (ruleCheckProgress == RuleCheckProgress::IN_PROGRESS) {
        --numCandidatesLeft;
        candidateIndex = *(pNextCandidate++);
        candidateAllocation = _finder->AllocationAt(candidateIndex);
        if (numCandidatesLeft == 0) {
          ruleCheckProgress = RuleCheckProgress::RULE_DONE;
        }
      }

      if (_visited[candidateIndex]) {
        continue;
      }

      if (!candidateAllocation->IsUsed() ||
          !rule._extensionSignatureChecker.Check(*candidateAllocation)) {
        continue;
      }
      if (rule._useOffsetInExtension) {
        if (rule._offsetInExtension + sizeof(Offset) >
            candidateAllocation->Size()) {
          continue;
        }
        if (rule._referenceIsOutgoing) {
          /*
           * We already covered the case above where both offsets are
           * relevant to an outgoing reference  but still have to make sure
           * that somewhere in the member allocation points to the exact
           * offset in the referenced allocation.
           */
          if (!rule._useOffsetInMember &&
              !AllocationHasAlignedPointer(
                  *memberAllocation,
                  candidateAllocation->Address() + rule._offsetInExtension)) {
            continue;
          }
        } else {
          if (rule._useOffsetInMember) {
            const char* image;
            Offset numBytesFound = _addressMap.FindMappedMemoryImage(
                candidateAllocation->Address() + rule._offsetInExtension,
                &image);
            if (numBytesFound < sizeof(Offset) ||
                *((Offset*)(image)) !=
                    memberAllocation->Address() + rule._offsetInMember) {
              continue;
            }
          }
        }
      } else {
        if (rule._useOffsetInMember && !rule._referenceIsOutgoing) {
          if (!AllocationHasAlignedPointer(
                  *candidateAllocation,
                  memberAllocation->Address() + rule._offsetInMember)) {
            continue;
          }
        }
      }

      /*
       * The point of this next part is that we don't want to bother pushing
       * context for a member for which all the rules have been checked.  This
       * is to save space taken by extensionContexts in the case of something
       * like a linked list, that may have a very long chain of extensions.
       */

      if (ruleCheckProgress != RuleCheckProgress::RULE_DONE ||
          ruleIndex + 1 != ruleIndexLimit) {
        extensionContexts.emplace(memberIndex, ruleIndex, numCandidatesLeft,
                                  ruleCheckProgress, pNextCandidate);
      }
      memberIndex = candidateIndex;
      memberAllocation = candidateAllocation;
      _visited[memberIndex] = true;
      visitor.Visit(memberIndex, *memberAllocation);
      state = rule._newState;
      ruleIndex = _stateToBase[state];
      ruleIndexLimit = _stateToBase[state + 1];
      if (ruleIndex != ruleIndexLimit) {
        ruleCheckProgress = RuleCheckProgress::NEW_RULE;
      } else {
        /*
         * The extension should not be enabled if the first state has no
         * rules, because none of the other extension states could ever be
         * reached.  If an extension state has no rules it must be a different
         * state, and so there must be at least the rules associated with the
         * base state before it.  Back up the rule index by 1 so we can advance
         * as part of the handling of RULE_DONE.
         */
        ruleIndex--;
        ruleCheckProgress = RuleCheckProgress::RULE_DONE;
      }
    }
  }

 private:
  struct Specification {
    Specification()
        : _offsetInMember(0),
          _offsetInExtension(0),
          _useOffsetInMember(false),
          _useOffsetInExtension(false),
          _referenceIsOutgoing(true),
          _baseState(0),
          _newState(0) {}
    size_t _offsetInMember;
    size_t _offsetInExtension;
    bool _useOffsetInMember;
    bool _useOffsetInExtension;
    bool _referenceIsOutgoing;
    std::string _memberSignature;
    std::string _extensionSignature;
    size_t _baseState;
    size_t _newState;
  };
  struct Rule {
    Rule(const SignatureDirectory<Offset>& directory,
         const VirtualAddressMap<Offset>& addressMap, const Specification& spec)
        : _offsetInMember(spec._offsetInMember),
          _offsetInExtension(spec._offsetInExtension),
          _useOffsetInMember(spec._useOffsetInMember),
          _useOffsetInExtension(spec._useOffsetInExtension),
          _referenceIsOutgoing(spec._referenceIsOutgoing),
          _memberSignatureChecker(directory, addressMap, spec._memberSignature),
          _extensionSignatureChecker(directory, addressMap,
                                     spec._extensionSignature),
          _baseState(spec._baseState),
          _newState(spec._newState) {}

    size_t _offsetInMember;
    size_t _offsetInExtension;
    bool _useOffsetInMember;
    bool _useOffsetInExtension;
    bool _referenceIsOutgoing;
    SignatureChecker<Offset> _memberSignatureChecker;
    SignatureChecker<Offset> _extensionSignatureChecker;
    size_t _baseState;
    size_t _newState;
  };
  bool AllocationHasAlignedPointer(const Allocation& allocation,
                                   Offset address) {
    Offset base = allocation.Address();
    const char* image;
    Offset numBytesFound = _addressMap.FindMappedMemoryImage(base, &image);
    Offset size = allocation.Size();
    Offset bytesToCheck = (numBytesFound < size) ? numBytesFound : size;
    // TODO: This is not correct in at least one non-linux case where
    // 0-filled pages may be omitted and thus the allocation image may
    // be non-contiguous in the core image.
    const Offset* limit =
        (const Offset*)(image + (bytesToCheck & ~(sizeof(Offset) - 1)));
    for (const Offset* nextCheck = (const Offset*)image; nextCheck < limit;
         nextCheck++) {
      if (*nextCheck == address) {
        return true;
      }
    }
    return false;
  }
  bool _isEnabled;
  bool _hasErrors;
  bool _canRun;
  const Graph<Offset>* _graph;
  const Finder<Offset>* _finder;
  const VirtualAddressMap<Offset>& _addressMap;
  AllocationIndex _numAllocations;
  std::vector<bool> _visited;
  std::vector<Rule> _rules;
  std::vector<size_t> _stateToBase;
};
}  // namespace Allocations
}  // namespace chap
