// Copyright (c) 2017-2021,2023,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <regex>
#include <stack>
#include "../Annotator.h"
#include "../AnnotatorRegistry.h"
#include "../CPlusPlus/TypeInfoDirectory.h"
#include "../Commands/Runner.h"
#include "../ProcessImage.h"
#include "Directory.h"
#include "EdgePredicate.h"
#include "Graph.h"
#include "PatternDescriberRegistry.h"
#include "Set.h"
#include "SignatureChecker.h"
#include "SignatureDirectory.h"
#include "TagHolder.h"

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
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  ExtendedVisitor(
      Commands::Context& context, const ProcessImage<Offset>& processImage,
      const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
      const AnnotatorRegistry<Offset>& annotatorRegistry,
      bool allowMissingSignatures, Set<Offset>& visited)
      : _context(context),
        _isEnabled(false),
        _hasErrors(false),
        _hasAnnotations(false),
        _allowMissingSignatures(allowMissingSignatures),
        _patternDescriberRegistry(patternDescriberRegistry),
        _annotatorRegistry(annotatorRegistry),
        _globalAnnotationSequence(annotatorRegistry),
        _graph(0),
        _directory(processImage.GetAllocationDirectory()),
        _addressMap(processImage.GetVirtualAddressMap()),
        _signatureDirectory(processImage.GetSignatureDirectory()),
        _typeInfoDirectory(processImage.GetTypeInfoDirectory()),
        _tagHolder(processImage.GetAllocationTagHolder()),
        _edgeIsTainted(processImage.GetEdgeIsTainted()),
        _edgeIsFavored(processImage.GetEdgeIsFavored()),
        _numAllocations(_directory.NumAllocations()),
        _visited(visited),
        _commentExtensions(false),
        _skipTaintedReferences(false),
        _skipUnfavoredReferences(false) {
    Commands::Error& error = context.GetError();
    size_t numExtendArguments = context.GetNumArguments("extend");
    size_t numAnnotateArguments = context.GetNumArguments("annotate");
    bool extensionNeeded = false;
    bool graphNeeded = false;
    if (numExtendArguments != 0) {
      extensionNeeded = true;
      graphNeeded = true;
      ProcessExtendArguments(context, error, numExtendArguments);
    }
    if (numAnnotateArguments != 0) {
      extensionNeeded = true;
      ProcessAnnotateArguments(context, error, numAnnotateArguments);
      _hasAnnotations = true;
    }
    if (extensionNeeded && !_hasErrors) {
      if (graphNeeded) {
        _graph = processImage.GetAllocationGraph();
        if (_graph == nullptr) {
          _hasErrors = true;
        }
      }
      _isEnabled = !_hasErrors;
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
      _visited.Add(memberIndex);
      visitor.Visit(memberIndex, allocation);
      return;
    }

    /*
     * If the extended visitor is enabled, but we already visited the given
     * set member as an extension to the set, don't visit it again, but possibly
     * add some comments to the output if commentExtensions is true.
     */

    if (_visited.Has(memberIndex)) {
      if (_commentExtensions) {
        _context.GetOutput()
            << "# Base set member at 0x" << std::hex << allocation.Address()
            << " was already visited via an extension rule.\n\n";
      }
      return;
    }

    /*
     * Visit the given member of the set before looking for any extensions.
     */

    _visited.Add(memberIndex);
    visitor.Visit(memberIndex, allocation);

    if (_hasAnnotations) {
      Annotate(memberIndex, allocation, 0);
    }

    if (_rules.empty()) {
      // There are no extension rules.
      return;
    }

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

          memberAllocation = _directory.AllocationAt(memberIndex);
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
        if (!rule._memberSignatureChecker.Check(memberIndex,
                                                *memberAllocation) ||
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

            candidateIndex = _directory.AllocationIndexOf(target);
            if (candidateIndex == _numAllocations) {
              continue;
            }
            candidateAllocation = _directory.AllocationAt(candidateIndex);
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
        candidateAllocation = _directory.AllocationAt(candidateIndex);
        if (numCandidatesLeft == 0) {
          ruleCheckProgress = RuleCheckProgress::RULE_DONE;
        }
      }

      bool alreadyVisited = _visited.Has(candidateIndex);
      if (!_commentExtensions && alreadyVisited) {
        continue;
      }

      if (rule._extensionMustBeLeaked && !(_graph->IsLeaked(candidateIndex))) {
        continue;
      }

      if (!candidateAllocation->IsUsed() ||
          !rule._extensionSignatureChecker.Check(candidateIndex,
                                                 *candidateAllocation)) {
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
          // incoming reference, use offset in extension.
          const char* image;
          Offset numBytesFound = _addressMap.FindMappedMemoryImage(
              candidateAllocation->Address() + rule._offsetInExtension, &image);
          Offset memberAddress = memberAllocation->Address();
          if (numBytesFound < sizeof(Offset)) {
            continue;
          }
          Offset pointerInCandidate = *((Offset*)(image));
          if (rule._useOffsetInMember) {
            if (pointerInCandidate != memberAddress + rule._offsetInMember) {
              continue;
            }
          } else {
            if ((pointerInCandidate < memberAddress) ||
                (pointerInCandidate >=
                 memberAddress + memberAllocation->Size())) {
              continue;
            }
          }
        }
      } else {
        // Don't use offset in extension.
        if (rule._useOffsetInMember && !rule._referenceIsOutgoing) {
          if (!AllocationHasAlignedPointer(
                  *candidateAllocation,
                  memberAllocation->Address() + rule._offsetInMember)) {
            continue;
          }
        }
      }

      if (_skipTaintedReferences &&
          (rule._referenceIsOutgoing
               ? _edgeIsTainted->For(memberIndex, candidateIndex)
               : _edgeIsTainted->For(candidateIndex, memberIndex))) {
        continue;
      }
      if (_skipUnfavoredReferences &&
          (rule._referenceIsOutgoing
               ? (_tagHolder->SupportsFavoredReferences(candidateIndex) &&
                  !_edgeIsFavored->For(memberIndex, candidateIndex))
               : (_tagHolder->SupportsFavoredReferences(memberIndex) &&
                  !_edgeIsFavored->For(candidateIndex, memberIndex)))) {
        continue;
      }
      if (_commentExtensions) {
        _context.GetOutput() << std::hex;
        if (rule._referenceIsOutgoing) {
          _context.GetOutput() << "# Allocation at 0x"
                               << memberAllocation->Address()
                               << " references allocation at 0x"
                               << candidateAllocation->Address() << ".\n";
        } else {
          _context.GetOutput() << "# Allocation at 0x"
                               << memberAllocation->Address()
                               << " is referenced by allocation at 0x"
                               << candidateAllocation->Address() << ".\n";
        }
        if (alreadyVisited) {
          _context.GetOutput() << "# Allocation at 0x"
                               << candidateAllocation->Address()
                               << " was already visited.\n";
          if (rule._newState != 0) {
            _context.GetOutput() << "# Allocation at 0x"
                                 << candidateAllocation->Address()
                                 << " would have been extended in state "
                                 << _stateLabels[rule._newState] << ".\n";
          }
          _context.GetOutput() << "\n";
          continue;
        }
        if (rule._newState != 0) {
          _context.GetOutput()
              << "# Allocation at 0x" << candidateAllocation->Address()
              << " will be extended in state " << _stateLabels[rule._newState]
              << ".\n";
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
      _visited.Add(memberIndex);
      visitor.Visit(memberIndex, *memberAllocation);

      state = rule._newState;
      if (_hasAnnotations) {
        Annotate(memberIndex, *memberAllocation, state);
      }
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
    bool _extensionMustBeLeaked;
    std::string _memberSignature;
    std::string _extensionSignature;
    size_t _baseState;
    size_t _newState;
  };
  struct Rule {
    Rule(const SignatureDirectory<Offset>& directory,
         const CPlusPlus::TypeInfoDirectory<Offset>& typeInfoDirectory,
         const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
         const VirtualAddressMap<Offset>& addressMap, const Specification& spec)
        : _offsetInMember(spec._offsetInMember),
          _offsetInExtension(spec._offsetInExtension),
          _useOffsetInMember(spec._useOffsetInMember),
          _useOffsetInExtension(spec._useOffsetInExtension),
          _referenceIsOutgoing(spec._referenceIsOutgoing),
          _extensionMustBeLeaked(spec._extensionMustBeLeaked),
          _memberSignatureChecker(directory, typeInfoDirectory,
                                  patternDescriberRegistry, addressMap,
                                  spec._memberSignature),
          _extensionSignatureChecker(directory, typeInfoDirectory,
                                     patternDescriberRegistry, addressMap,
                                     spec._extensionSignature),
          _baseState(spec._baseState),
          _newState(spec._newState) {}

    size_t _offsetInMember;
    size_t _offsetInExtension;
    bool _useOffsetInMember;
    bool _useOffsetInExtension;
    bool _referenceIsOutgoing;
    bool _extensionMustBeLeaked;
    SignatureChecker<Offset> _memberSignatureChecker;
    SignatureChecker<Offset> _extensionSignatureChecker;
    size_t _baseState;
    size_t _newState;
  };
  void ProcessExtendArguments(Commands::Context& context,
                              Commands::Error& error,
                              size_t numExtendArguments) {
    if (!context.ParseBooleanSwitch("commentExtensions", _commentExtensions)) {
      _hasErrors = true;
    }
    if (_edgeIsTainted != nullptr &&
        !context.ParseBooleanSwitch("skipTaintedReferences",
                                    _skipTaintedReferences)) {
      _hasErrors = true;
    }
    if (_edgeIsFavored != nullptr &&
        !context.ParseBooleanSwitch("skipUnfavoredReferences",
                                    _skipUnfavoredReferences)) {
      _hasErrors = true;
    }

    //  [signature-or-pattern-or-label][@offset-in-member]<direction
    //  indicator>[signature][@offset-in-extension][=>stateLabel]
    std::regex extensionRegex(
        "([^@]*)(@([[:xdigit:]]+))?"   // specify the starting members
        "((->)|(~>)|(<-))"             // extend by outgoing or incoming refs
        "([^@=]*)(@([[:xdigit:]]+))?"  // constrain the type of extension
        "(=>(\\w+))?");                // select a new extension state
    std::smatch extensionSmatch;
    _stateLabels.push_back("");
    _labelToStateNumber[""] = 0;
    std::vector<Specification> specifications;
    specifications.reserve(numExtendArguments);
    for (size_t i = 0; i < numExtendArguments; i++) {
      const std::string extensionRule = context.Argument("extend", i);
      if (!std::regex_match(extensionRule, extensionSmatch, extensionRegex)) {
        error << "Extension specification \"" << extensionRule
              << "\" is ill formed.\n";
        _hasErrors = true;
        continue;
      }
      Specification& spec = specifications.emplace_back();
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
      spec._referenceIsOutgoing = (extensionSmatch[5].length() > 0) ||
                                  (extensionSmatch[6].length() > 0);
      spec._extensionMustBeLeaked = (extensionSmatch[6].length() > 0);
      spec._extensionSignature = extensionSmatch[8];
      if (extensionSmatch[10].length() > 0) {
        spec._useOffsetInExtension = true;
        size_t offsetInExtension;
        std::istringstream is(extensionSmatch[10]);
        is >> std::hex >> offsetInExtension;
        if (!is.fail() && is.eof()) {
          spec._offsetInExtension = offsetInExtension;
        } else {
          error << "Offset in extension \"" << extensionSmatch[10]
                << " is not well formed as hexadecimal.\n";
          _hasErrors = true;
        }
      }

      std::string stateLabel = extensionSmatch[12];
      size_t stateIndex = 0;
      std::map<std::string, size_t>::iterator it =
          _labelToStateNumber.find(stateLabel);
      if (it != _labelToStateNumber.end()) {
        stateIndex = it->second;
      } else {
        stateIndex = _labelToStateNumber.size();
        _stateLabels.push_back(stateLabel);
        _labelToStateNumber[stateLabel] = stateIndex;
      }
      spec._newState = stateIndex;
    }

    /*
     * Now that all the state names are known, identify any cases where a
     * state label was provided instead of a member signature.  Don't bother
     * with specifications that were already rejected as ill formed.
     */

    size_t numStates = _labelToStateNumber.size();
    _stateToBase.resize(numStates + 1, 0);
    for (typename std::vector<Specification>::iterator it =
             specifications.begin();
         it != specifications.end(); ++it) {
      Specification& spec = *it;
      if (!spec._memberSignature.empty()) {
        std::map<std::string, size_t>::const_iterator itState =
            _labelToStateNumber.find(spec._memberSignature);
        if (itState != _labelToStateNumber.end()) {
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

    /*
     * Create the extension rules in the calculated order.
     */

    _rules.reserve(numSpecs);
    for (size_t i = 0; i < numSpecs; i++) {
      _rules.emplace_back(_signatureDirectory, _typeInfoDirectory,
                          _patternDescriberRegistry, _addressMap,
                          specifications[ruleIndexToArgumentIndex[i]]);
      Rule& rule = _rules.back();
      if (rule._memberSignatureChecker.UnrecognizedSignature()) {
        if (!_allowMissingSignatures) {
          error << "Member signature \""
                << rule._memberSignatureChecker.GetSignature()
                << "\" is not recognized.\n";
          _hasErrors = true;
        }
      }
      if (rule._memberSignatureChecker.UnrecognizedPattern()) {
        error << "Member pattern \""
              << rule._memberSignatureChecker.GetPatternName()
              << "\" is not recognized.\n";
        _hasErrors = true;
      }
      if (rule._extensionSignatureChecker.UnrecognizedSignature()) {
        if (!_allowMissingSignatures) {
          error << "Extension signature \""
                << rule._extensionSignatureChecker.GetSignature()
                << "\" is not recognized.\n";
          _hasErrors = true;
        }
      }
      if (rule._extensionSignatureChecker.UnrecognizedPattern()) {
        error << "Extension pattern \""
              << rule._extensionSignatureChecker.GetPatternName()
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
      }
    }
  }

   /*
    * Allocators are ordered in the order in which they are added, unless
    * AddAllAnnotators has been called, in which case they are in the
    * order that they were registered.
    */

  class OrderedAnnotators {
   public:
    OrderedAnnotators(const AnnotatorRegistry<Offset>& annotatorRegistry)
      : _usesAllAnnotators(false), _annotatorRegistry(annotatorRegistry) {}
    void AddAnnotator(const Annotator<Offset>* annotator) {
      if (!_usesAllAnnotators && _used.insert(annotator).second) {
        _inOrderAdded.push_back(annotator);
      }
    }
    void AddAllAnnotators() {
      if (!_usesAllAnnotators) {
        _used.clear();
        _inOrderAdded.clear();
        for (const Annotator<Offset>* annotator :
             _annotatorRegistry.Annotators()) {
          _used.insert(annotator);
          _inOrderAdded.push_back(annotator);
        }
        _usesAllAnnotators = true;
      }
    }

    const std::vector<const Annotator<Offset>*>& InOrderAdded() const {
      return _inOrderAdded;
    }

   private:
    bool _usesAllAnnotators;
    std::unordered_set<const Annotator<Offset>*> _used;
    std::vector<const Annotator<Offset>*> _inOrderAdded;
    const AnnotatorRegistry<Offset>& _annotatorRegistry;
  };

  struct AnnotationSequence {
    AnnotationSequence(const AnnotatorRegistry<Offset>& annotatorRegistry)
        : _isEmpty(true),
          _tryAllAnnotationsEverywhere(false),
          _annotatorRegistry(annotatorRegistry),
          _allocationWideAnnotators(annotatorRegistry) {}

    void AddAnnotator(const Annotator<Offset>* annotator) {
      _isEmpty = false;
      if (_tryAllAnnotationsEverywhere) {
        return;
      }
      if (annotator == nullptr) {
        _tryAllAnnotationsEverywhere = true;
        _offsetSpecificAnnotators.clear();
        _allocationWideAnnotators.AddAllAnnotators();
      } else {
        _allocationWideAnnotators.AddAnnotator(annotator);
      }
    }

    void AddAnnotator(const Annotator<Offset>* annotator, Offset fieldOffset) {
      _isEmpty = false;
      if (_tryAllAnnotationsEverywhere) {
        return;
      }

      OrderedAnnotators& orderedAnnotators =
          _offsetSpecificAnnotators.try_emplace(fieldOffset, _annotatorRegistry)
              .first->second;
      if (annotator == nullptr) {
        orderedAnnotators.AddAllAnnotators();
      } else {
        orderedAnnotators.AddAnnotator(annotator);
      }
    }

    bool _isEmpty;
    bool _tryAllAnnotationsEverywhere;
    const AnnotatorRegistry<Offset>& _annotatorRegistry;
    OrderedAnnotators _allocationWideAnnotators;
    std::map<Offset, OrderedAnnotators> _offsetSpecificAnnotators;
  };

  struct SignatureCheckerWithAnnotationSequence {
    SignatureCheckerWithAnnotationSequence(
        const SignatureDirectory<Offset>& signatureDirectory,
        const CPlusPlus::TypeInfoDirectory<Offset>& typeInfoDirectory,
        const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
        const VirtualAddressMap<Offset>& addressMap,
        const std::string& signature, AnnotationSequence* annotationSequence)
        : _signatureChecker(signatureDirectory, typeInfoDirectory,
                            patternDescriberRegistry, addressMap, signature),
          _annotationSequence(annotationSequence) {}

    SignatureChecker<Offset> _signatureChecker;
    AnnotationSequence* _annotationSequence;
  };

  void ProcessAnnotateArguments(Commands::Context& context,
                                Commands::Error& error,
                                size_t numAnnotateArguments) {
    std::regex annotationRegex(
        "((\\*)|([^@]+))((@([[:xdigit:]]+))?)"  // what to annotate
        "\\."
        "((\\*)|([^@.]+))");

    _stateToAnnotationSequence.resize(_labelToStateNumber.size() + 1, nullptr);
    std::smatch annotationSmatch;
    for (size_t i = 0; i < numAnnotateArguments; i++) {
      const std::string annotationRule = context.Argument("annotate", i);
      if (!std::regex_match(annotationRule, annotationSmatch,
                            annotationRegex)) {
        error << "Annotation specification \"" << annotationRule
              << "\" is ill formed.\n";
        _hasErrors = true;
        continue;
      }
      std::string constraint = annotationSmatch[1];
      bool globalAnnotation = annotationSmatch[2].matched;
      AnnotationSequence& annotationSequence =
          globalAnnotation ? _globalAnnotationSequence
                           : _constraintToAnnotationSequence
                                 .try_emplace(constraint, _annotatorRegistry)
                                 .first->second;
      bool fieldOffsetSupplied = annotationSmatch[5].matched;
      Offset fieldOffset = 0;
      if (fieldOffsetSupplied) {
        std::string fieldOffsetString = annotationSmatch[6];
        std::istringstream is(fieldOffsetString);
        is >> std::hex >> fieldOffset;
        if (is.fail() || !is.eof()) {
          error << "\"" << fieldOffsetString
                << "\" is not a valid hexadecimal field offset.\n";
          _hasErrors = true;
        }
      }
      std::string annotatorName = annotationSmatch[7];

      const Annotator<Offset>* annotator = nullptr;
      if (annotationSmatch[9].matched) {
        annotator = _annotatorRegistry.FindAnnotator(annotatorName);
        if (annotator == nullptr) {
          error << "\"" << annotatorName
                << "\" is not a valid annotator name.\n";
          _hasErrors = true;
        }
      }
      if (fieldOffsetSupplied) {
        annotationSequence.AddAnnotator(annotator, fieldOffset);
      } else {
        annotationSequence.AddAnnotator(annotator);
      }

      if (globalAnnotation) {
        continue;
      }

      std::map<std::string, size_t>::iterator it =
          _labelToStateNumber.find(constraint);
      if (it != _labelToStateNumber.end()) {
        _stateToAnnotationSequence[it->second] = &annotationSequence;
        continue;
      }
      _signatureCheckersWithAnnotationSequences.emplace_back(
          _signatureDirectory, _typeInfoDirectory, _patternDescriberRegistry,
          _addressMap, constraint, &annotationSequence);

      SignatureChecker<Offset>& signatureChecker =
         _signatureCheckersWithAnnotationSequences.back()._signatureChecker;
      if (signatureChecker.UnrecognizedSignature()) {
        if (!_allowMissingSignatures) {
          error << "Annotation constraint \""
                << constraint
                << "\" is not recognized as a signature.\n";
          _hasErrors = true;
        }
      }
      if (signatureChecker.UnrecognizedPattern()) {
        error << "Annotation constraint pattern \"" << constraint
              << "\" is not recognized.\n";
        _hasErrors = true;
      }
    }

    if (_hasErrors) {
      return;
    }

    if (_globalAnnotationSequence._tryAllAnnotationsEverywhere) {
      _constraintToAnnotationSequence.clear();
      _signatureCheckersWithAnnotationSequences.clear();
      _stateToAnnotationSequence.clear();
    }
  }

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

  void Annotate(AllocationIndex memberIndex, const Allocation& allocation,
                size_t state) {
    bool tryAllAnnotationsEverywhere = false;
    std::list<AnnotationSequence*> annotationSequences;
    if (_globalAnnotationSequence._tryAllAnnotationsEverywhere) {
      annotationSequences.push_back(&_globalAnnotationSequence);
      tryAllAnnotationsEverywhere = true;
    }
    AnnotationSequence* annotationSequenceForState =
        _stateToAnnotationSequence[state];
    if (annotationSequenceForState != nullptr && !tryAllAnnotationsEverywhere &&
        annotationSequenceForState->_tryAllAnnotationsEverywhere) {
      annotationSequences.push_back(annotationSequenceForState);
      tryAllAnnotationsEverywhere = true;
    }

    if (!tryAllAnnotationsEverywhere) {
      for (const auto& signatureCheckerWithAnnotationSequence:
              _signatureCheckersWithAnnotationSequences ) {
        if (signatureCheckerWithAnnotationSequence._signatureChecker.Check(
                memberIndex, allocation)) {
          AnnotationSequence* annotationSequence =
              signatureCheckerWithAnnotationSequence._annotationSequence;
          if (annotationSequence->_tryAllAnnotationsEverywhere) {
            annotationSequences.clear();
            annotationSequences.push_back(annotationSequence);
            tryAllAnnotationsEverywhere = true;
          } else {
            annotationSequences.push_back(annotationSequence);
          }
        }
      }
    }
    if (!tryAllAnnotationsEverywhere) {
      if (annotationSequenceForState != nullptr &&
          !annotationSequenceForState->_isEmpty) {
        annotationSequences.push_front(annotationSequenceForState);
      }
      if (!_globalAnnotationSequence._isEmpty) {
        annotationSequences.push_back(&_globalAnnotationSequence);
      }
    }
    if (annotationSequences.empty()) {
      return;
    }

    typename VirtualAddressMap<Offset>::Reader reader(_addressMap);
    Offset address = allocation.Address();
    Offset annotateFrom = address;
    Offset allocationSize = allocation.Size();
    Offset annotateTo = address + allocationSize;

    typename Annotator<Offset>::WriteHeaderFunction writeHeader = [&](
        Offset start, Offset limit, const std::string& name) {
      _context.GetOutput() << " Annotator " << name << " matches [0x"
                           << std::hex << address << " + 0x"
                           << (start - address) << ", 0x" << address << " + 0x"
                           << (limit - address) << ")\n";
    };

    while (annotateFrom < annotateTo) {
      bool annotationDone = false;
      for (const auto annotationSequence : annotationSequences) {
        auto it = annotationSequence->_offsetSpecificAnnotators.find(
            annotateFrom - address);
        if (it != annotationSequence->_offsetSpecificAnnotators.end()) {
          for (auto annotator : it->second.InOrderAdded()) {
            Offset endOfAnnotation = annotator->Annotate(
                _context, reader, writeHeader, annotateFrom, annotateTo, "   ");
            if (endOfAnnotation != annotateFrom) {
              annotationDone = true;
              annotateFrom = endOfAnnotation;
              _context.GetOutput() << "\n";
              break;
            }
          }
        }
        if (annotationDone) {
          break;
        }
        for (auto annotator :
             annotationSequence->_allocationWideAnnotators.InOrderAdded()) {
          Offset endOfAnnotation = annotator->Annotate(
              _context, reader, writeHeader, annotateFrom, annotateTo, "   ");
          if (endOfAnnotation != annotateFrom) {
            annotationDone = true;
            annotateFrom = endOfAnnotation;
            _context.GetOutput() << "\n";
            break;
          }
        }
        if (annotationDone) {
          break;
        }
      }
      if (!annotationDone) {
        annotateFrom += sizeof(Offset);
      }
    }
  }

  Commands::Context& _context;
  bool _isEnabled;
  bool _hasErrors;
  bool _hasAnnotations;
  bool _allowMissingSignatures;
  const PatternDescriberRegistry<Offset>& _patternDescriberRegistry;
  const AnnotatorRegistry<Offset>& _annotatorRegistry;
  AnnotationSequence _globalAnnotationSequence;
  const Graph<Offset>* _graph;
  const Directory<Offset>& _directory;
  const VirtualAddressMap<Offset>& _addressMap;
  const SignatureDirectory<Offset>& _signatureDirectory;
  const CPlusPlus::TypeInfoDirectory<Offset>& _typeInfoDirectory;
  const TagHolder<Offset>* _tagHolder;
  const EdgePredicate<Offset>* _edgeIsTainted;
  const EdgePredicate<Offset>* _edgeIsFavored;
  AllocationIndex _numAllocations;
  Set<Offset>& _visited;
  std::vector<Rule> _rules;
  std::vector<size_t> _stateToBase;
  bool _commentExtensions;
  bool _skipTaintedReferences;
  bool _skipUnfavoredReferences;
  std::vector<std::string> _stateLabels;
  std::map<std::string, size_t> _labelToStateNumber;
  std::list<SignatureCheckerWithAnnotationSequence>
      _signatureCheckersWithAnnotationSequences;

  std::unordered_map<std::string, AnnotationSequence>
      _constraintToAnnotationSequence;
  std::vector<AnnotationSequence*> _stateToAnnotationSequence;
};
}  // namespace Allocations
}  // namespace chap
