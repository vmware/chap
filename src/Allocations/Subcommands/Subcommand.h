// Copyright (c) 2017-2021,2023,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <iostream>
#include <memory>
#include <sstream>
#include "../../AnnotatorRegistry.h"
#include "../../CPlusPlus/TypeInfoDirectory.h"
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Directory.h"
#include "../EdgePredicate.h"
#include "../ExtendedVisitor.h"
#include "../PatternDescriberRegistry.h"
#include "../ReferenceConstraint.h"
#include "../SetCache.h"
#include "../SignatureChecker.h"
#include "../TagHolder.h"
namespace chap {
namespace Allocations {
namespace Subcommands {
template <class Offset, class Visitor, class Iterator>
class Subcommand : public Commands::Subcommand {
 public:
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  Subcommand(const ProcessImage<Offset>& processImage,
             typename Visitor::Factory& visitorFactory,
             typename Iterator::Factory& iteratorFactory,
             const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
             const AnnotatorRegistry<Offset>& annotatorRegistry,
             SetCache<Offset>& setCache)
      : Commands::Subcommand(visitorFactory.GetCommandName(),
                             iteratorFactory.GetSetName()),
        _visitorFactory(visitorFactory),
        _iteratorFactory(iteratorFactory),
        _patternDescriberRegistry(patternDescriberRegistry),
        _annotatorRegistry(annotatorRegistry),
        _setCache(setCache),
        _processImage(processImage) {}

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    Commands::Error& error = context.GetError();
    bool isRedirected = context.IsRedirected();
    const Directory<Offset>& directory = _processImage.GetAllocationDirectory();
    AllocationIndex numAllocations = directory.NumAllocations();

    std::unique_ptr<Iterator> iterator;
    iterator.reset(_iteratorFactory.MakeIterator(context, _processImage,
                                                 directory, _setCache));
    if (iterator.get() == 0) {
      return;
    }

    size_t numPositionals = context.GetNumPositionals();
    size_t nextPositional = 2 + _iteratorFactory.GetNumArguments();

    const SignatureDirectory<Offset>& signatureDirectory =
        _processImage.GetSignatureDirectory();
    const CPlusPlus::TypeInfoDirectory<Offset>& typeInfoDirectory =
        _processImage.GetTypeInfoDirectory();
    const VirtualAddressMap<Offset>& addressMap =
        _processImage.GetVirtualAddressMap();

    std::string signatureString;
    if (nextPositional < numPositionals) {
      size_t signaturePositional = nextPositional++;
      if (nextPositional < numPositionals) {
        error << "Unexpected positional arguments found:\n";
        do {
          error << "\"" << context.Positional(nextPositional++) << "\"\n";
        } while (nextPositional < numPositionals);
        return;
      }
      signatureString = context.Positional(signaturePositional);
    }

    bool skipTaintedReferences = false;
    if (!context.ParseBooleanSwitch("skipTaintedReferences",
                                    skipTaintedReferences)) {
      return;
    }

    bool skipUnfavoredReferences = false;
    if (!context.ParseBooleanSwitch("skipUnfavoredReferences",
                                    skipUnfavoredReferences)) {
      return;
    }

    bool signatureOrPatternError = false;
    SignatureChecker<Offset> signatureChecker(
        signatureDirectory, typeInfoDirectory, _patternDescriberRegistry,
        addressMap, signatureString);
    bool switchError = false;
    bool allowMissingSignatures = false;
    if (!context.ParseBooleanSwitch("allowMissingSignatures",
                                    allowMissingSignatures)) {
      switchError = true;
    }

    if (signatureChecker.UnrecognizedSignature()) {
      if (!allowMissingSignatures) {
        error << "Signature \"" << signatureString << "\" is not recognized.\n";
        signatureOrPatternError = true;
      }
    }
    if (signatureChecker.UnrecognizedPattern()) {
      error << "Pattern \"" << signatureChecker.GetPatternName()
            << "\" is not recognized.\n";
      signatureOrPatternError = true;
    }

    Offset minSize = 0;
    Offset maxSize = ~((Offset)0);

    /*
     * It generally does not make sense to specify more than one
     * size argument or more than one minsize argument or more than
     * one maxsize argument but for now this is treated as harmless,
     * just forcing all the constraints to apply.
     */

    size_t numSizeArguments = context.GetNumArguments("size");
    for (size_t i = 0; i < numSizeArguments; i++) {
      Offset size;
      if (!context.ParseArgument("size", i, size)) {
        switchError = true;
      } else {
        if (minSize < size) {
          minSize = size;
        }
        if (maxSize > size) {
          maxSize = size;
        }
      }
    }
    size_t numMinSizeArguments = context.GetNumArguments("minsize");
    for (size_t i = 0; i < numMinSizeArguments; i++) {
      Offset size;
      if (!context.ParseArgument("minsize", i, size)) {
        switchError = true;
      } else {
        if (minSize < size) {
          minSize = size;
        }
      }
    }
    size_t numMaxSizeArguments = context.GetNumArguments("maxsize");
    for (size_t i = 0; i < numMaxSizeArguments; i++) {
      Offset size;
      if (!context.ParseArgument("maxsize", i, size)) {
        switchError = true;
      } else {
        if (maxSize > size) {
          maxSize = size;
        }
      }
    }

    Offset geometricSampleBase = 0;
    size_t numGeometricSampleArguments =
        context.GetNumArguments("geometricSample");
    if (numGeometricSampleArguments > 0) {
      if (numGeometricSampleArguments > 1) {
        std::cerr << "At most one /geometricSample switch is allowed.\n";
        switchError = true;
      }
      const std::string& geometricSampleBaseString =
          context.Argument("geometricSample", 0);
      geometricSampleBase = atoi(geometricSampleBaseString.c_str());
      if (geometricSampleBase == 0) {
        if (geometricSampleBaseString[0] != '0' ||
            geometricSampleBaseString[1] != '\000') {
          error << "Invalid decimal geometric sample base: "
                << "\"" << geometricSampleBaseString << "\".\n";
          switchError = true;
        }
      }
    }

    bool assignDefault = false;
    bool subtractFromDefault = false;

    size_t numSetOperationArguments = context.GetNumArguments("setOperation");
    if (numSetOperationArguments > 0) {
      if (numSetOperationArguments > 1) {
        std::cerr << "At most one /setOperation switch is allowed.\n";
        switchError = true;
      }
      const std::string& operation = context.Argument("setOperation", 0);
      if (operation == "assign") {
        assignDefault = true;
      } else if (operation == "subtract") {
        subtractFromDefault = true;
      } else {
        std::cerr << "Set operation " << operation << " is not supported.\n";
        switchError = true;
      }
    }

    Set<Offset>& visited = _setCache.GetVisited();

    std::vector<ReferenceConstraint<Offset> > referenceConstraints;
    const Graph<Offset>* graph = _processImage.GetAllocationGraph();
    const EdgePredicate<Offset>* edgeIsTainted =
        _processImage.GetEdgeIsTainted();
    const EdgePredicate<Offset>* edgeIsFavored =
        _processImage.GetEdgeIsFavored();
    const TagHolder<Offset>* tagHolder = _processImage.GetAllocationTagHolder();

    size_t numMinIncoming = context.GetNumArguments("minincoming");
    size_t numMaxIncoming = context.GetNumArguments("maxincoming");
    size_t numMinOutgoing = context.GetNumArguments("minoutgoing");
    size_t numMaxOutgoing = context.GetNumArguments("maxoutgoing");
    size_t numMinFreeOutgoing = context.GetNumArguments("minfreeoutgoing");
    size_t numReferenceConstraints = numMinIncoming + numMaxIncoming +
                                     numMinOutgoing + numMaxOutgoing +
                                     numMinFreeOutgoing;
    if (numReferenceConstraints > 0) {
      referenceConstraints.reserve(numReferenceConstraints);
      if (graph == 0) {
        std::cerr
            << "Constraints were placed on incoming or outgoing references\n"
               "but it was not possible to calculate the graph.";
        return;
      }

      switchError =
          switchError |
          AddReferenceConstraints(
              context, "minincoming", ReferenceConstraint<Offset>::MINIMUM,
              ReferenceConstraint<Offset>::INCOMING, true, directory, *graph,
              signatureDirectory, typeInfoDirectory, addressMap,
              referenceConstraints, allowMissingSignatures, *tagHolder,
              skipTaintedReferences, *edgeIsTainted, skipUnfavoredReferences,
              *edgeIsFavored);
      switchError =
          switchError |
          AddReferenceConstraints(
              context, "maxincoming", ReferenceConstraint<Offset>::MAXIMUM,
              ReferenceConstraint<Offset>::INCOMING, true, directory, *graph,
              signatureDirectory, typeInfoDirectory, addressMap,
              referenceConstraints, allowMissingSignatures, *tagHolder,
              skipTaintedReferences, *edgeIsTainted, skipUnfavoredReferences,
              *edgeIsFavored);
      switchError =
          switchError |
          AddReferenceConstraints(
              context, "minoutgoing", ReferenceConstraint<Offset>::MINIMUM,
              ReferenceConstraint<Offset>::OUTGOING, true, directory, *graph,
              signatureDirectory, typeInfoDirectory, addressMap,
              referenceConstraints, allowMissingSignatures, *tagHolder,
              skipTaintedReferences, *edgeIsTainted, skipUnfavoredReferences,
              *edgeIsFavored);
      switchError =
          switchError |
          AddReferenceConstraints(
              context, "maxoutgoing", ReferenceConstraint<Offset>::MAXIMUM,
              ReferenceConstraint<Offset>::OUTGOING, true, directory, *graph,
              signatureDirectory, typeInfoDirectory, addressMap,
              referenceConstraints, allowMissingSignatures, *tagHolder,
              skipTaintedReferences, *edgeIsTainted, skipUnfavoredReferences,
              *edgeIsFavored);
      switchError =
          switchError |
          AddReferenceConstraints(
              context, "minfreeoutgoing", ReferenceConstraint<Offset>::MINIMUM,
              ReferenceConstraint<Offset>::OUTGOING, false, directory, *graph,
              signatureDirectory, typeInfoDirectory, addressMap,
              referenceConstraints, allowMissingSignatures, *tagHolder,
              skipTaintedReferences, *edgeIsTainted, skipUnfavoredReferences,
              *edgeIsFavored);
    }

    ExtendedVisitor<Offset, Visitor> extendedVisitor(
        context, _processImage, _patternDescriberRegistry, _annotatorRegistry,
        allowMissingSignatures, visited);
    if (extendedVisitor.HasErrors() || switchError || signatureOrPatternError) {
      return;
    }
    std::unique_ptr<Visitor> visitor;
    visitor.reset(_visitorFactory.MakeVisitor(context, _processImage));
    if (visitor.get() == 0) {
      return;
    }
    Visitor& visitorRef = *(visitor.get());

    const std::vector<std::string>& taints = _iteratorFactory.GetTaints();
    if (!taints.empty()) {
      error << "The output of this command cannot be trusted:\n";
      if (isRedirected) {
        output << "The output of this command cannot be trusted:\n";
      }
      for (std::vector<std::string>::const_iterator it = taints.begin();
           it != taints.end(); ++it) {
        error << *it;
        if (isRedirected) {
          error << *it;
        }
      }
    }

    Offset nextInGeometricSample = 0;
    if (geometricSampleBase != 0) {
      nextInGeometricSample = 1;
    }
    Offset numSeenInBaseSet = 0;
    visited.Clear();
    for (AllocationIndex index = iterator->Next(); index != numAllocations;
         index = iterator->Next()) {
      const Allocation* allocation = directory.AllocationAt(index);
      if (allocation == 0) {
        abort();
      }
      Offset size = allocation->Size();

      if (size < minSize || size > maxSize) {
        continue;
      }

      if (!signatureChecker.Check(index, *allocation)) {
        continue;
      }

      bool unsatisfiedReferenceConstraint = false;
      for (auto constraint : referenceConstraints) {
        if (!constraint.Check(index)) {
          unsatisfiedReferenceConstraint = true;
        }
      }
      if (unsatisfiedReferenceConstraint) {
        continue;
      }

      ++numSeenInBaseSet;
      if (nextInGeometricSample != 0) {
        if (numSeenInBaseSet != nextInGeometricSample) {
          continue;
        }
        nextInGeometricSample *= geometricSampleBase;
      }

      extendedVisitor.Visit(index, *allocation, visitorRef);
    }
    if (assignDefault) {
      _setCache.GetDerived().Assign(visited);
    } else if (subtractFromDefault) {
      _setCache.GetDerived().Subtract(visited);
    }
  }

  void ShowHelpMessage(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    _visitorFactory.ShowHelpMessage(context);
    output << "\n";
    _iteratorFactory.ShowHelpMessage(context);
    output
        << "\nThe set can be further restricted by appending a class"
           " name or any value\n"
           "in hexadecimal to match against the first "
        << std::dec << ((sizeof(Offset) * 8))
        << "-bit unsigned word, or by specifying\n\"-\" to accept"
           " only unsigned allocations.\n\n"
           "It can also be further restricted by any of the following"
           " switches:\n\n"
           "/minsize <size-in-hex> imposes a minimum size.\n"
           "/maxsize <size-in-hex> imposes a maximum size.\n"
           "/size <size-in-hex> imposes an exact size requirement.\n\n"
           "/minincoming [<signature>=]<count> restricts that each member"
           " must have at least\n"
           " the specified number of incoming references, if"
           " no signature is specified, or\n"
           " at least the specified number"
           " of incoming references from allocations with the\n"
           " specified signature.\n"
           "/maxincoming is like /minincoming, but imposes a maximum.\n"
           "/minoutgoing is like /minincoming, but for outgoing references.\n"
           "/maxoutgoing is like /maxincoming, but for outgoing references.\n"
           "/minfreeoutgoing is like /minoutgoing, but for references to"
           " free allocations,\n"
           " with the caveat that normally such references are false, so this"
           " switch cannot\n"
           " be used for automated bug detection.\n\n"
           "/geometricSample <base-in-decimal> causes only entries 1, b, "
           "b**2, b**3...\n to be visited.\n\n"
           "After restrictions have been applied, the /extend switch can be"
           " used to extend\n"
           " the set to adjacent allocations.  See USERGUIDE.md for details.\n";
  }

 private:
  typename Visitor::Factory& _visitorFactory;
  typename Iterator::Factory& _iteratorFactory;
  const PatternDescriberRegistry<Offset>& _patternDescriberRegistry;
  const AnnotatorRegistry<Offset>& _annotatorRegistry;
  SetCache<Offset>& _setCache;
  const ProcessImage<Offset>& _processImage;

  bool AddReferenceConstraints(
      Commands::Context& context, const std::string& switchName,
      typename ReferenceConstraint<Offset>::BoundaryType boundaryType,
      typename ReferenceConstraint<Offset>::ReferenceType referenceType,
      bool wantUsed, const Directory<Offset>& directory,
      const Graph<Offset>& graph,
      const SignatureDirectory<Offset>& signatureDirectory,
      const CPlusPlus::TypeInfoDirectory<Offset>& typeInfoDirectory,
      const VirtualAddressMap<Offset>& addressMap,
      std::vector<ReferenceConstraint<Offset> >& constraints,
      bool allowMissingSignatures, const TagHolder<Offset>& tagHolder,
      bool skipTaintedReferences, const EdgePredicate<Offset>& edgeIsTainted,
      bool skipUnfavoredReferences,
      const EdgePredicate<Offset>& edgeIsFavored) {
    bool switchError = false;
    size_t numSwitches = context.GetNumArguments(switchName);
    Commands::Error& error = context.GetError();
    for (size_t i = 0; i < numSwitches; ++i) {
      const std::string& signatureAndCount = context.Argument(switchName, i);
      const char* countString = signatureAndCount.c_str();
      // If there is no embedded "=", no signature is wanted and only a count
      // is specified.
      std::string signature;

      size_t equalPos = signatureAndCount.find("=");
      if (equalPos != signatureAndCount.npos) {
        countString += (equalPos + 1);
        signature = signatureAndCount.substr(0, equalPos);
      }
      size_t count = atoi(countString);
      if (count == 0) {
        if (countString[0] != '0' || countString[1] != '\000') {
          error << "Invalid count "
                << "\"" << countString << "\".\n";
          switchError = true;
        }
      }
      constraints.emplace_back(
          signatureDirectory, typeInfoDirectory, _patternDescriberRegistry,
          addressMap, signature, count, wantUsed, boundaryType, referenceType,
          directory, graph, tagHolder, skipTaintedReferences, edgeIsTainted,
          skipUnfavoredReferences, edgeIsFavored);
      if (constraints.back().UnrecognizedSignature()) {
        if (!allowMissingSignatures) {
          error << "Signature \"" << signature << "\" is not recognized.\n";
          switchError = true;
        }
      }
      if (constraints.back().UnrecognizedPattern()) {
        error << "Pattern \"" << signature.substr(1)
              << "\" is not recognized.\n";
        switchError = true;
      }
    }
    return switchError;
  }
};
}  // namespace Subcommands
}  // namespace Allocations
}  // namespace chap
