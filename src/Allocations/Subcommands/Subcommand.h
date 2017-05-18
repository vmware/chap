// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <memory>
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../Finder.h"
namespace chap {
namespace Allocations {
namespace Subcommands {
template <class Offset, class Visitor, class Iterator>
class Subcommand : public Commands::Subcommand {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  Subcommand(typename Visitor::Factory& visitorFactory,
             typename Iterator::Factory& iteratorFactory)
      : Commands::Subcommand(visitorFactory.GetCommandName(),
                             iteratorFactory.GetSetName()),
        _visitorFactory(visitorFactory),
        _iteratorFactory(iteratorFactory),
        _processImage(0) {}

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    Commands::Error& error = context.GetError();
    bool isRedirected = context.IsRedirected();
    if (_processImage == 0) {
      error << "This command is currently disabled.\n";
      error << "There is no process image.\n";
      if (isRedirected) {
        output << "This command is currently disabled.\n";
        output << "There is no process image.\n";
      }
      return;
    }
    const Finder<Offset>* allocationFinder =
        _processImage->GetAllocationFinder();
    if (allocationFinder == 0) {
      error << "This command is currently disabled.\n";
      error << "Allocations cannot be found.\n";
      if (isRedirected) {
        output << "This command is currently disabled.\n";
        output << "Allocations cannot be found.\n";
      }
      return;
    }
    AllocationIndex numAllocations = allocationFinder->NumAllocations();

    std::unique_ptr<Iterator> iterator;
    iterator.reset(_iteratorFactory.MakeIterator(context, *_processImage,
                                                 *allocationFinder));
    if (iterator.get() == 0) {
      return;
    }

    size_t numPositionals = context.GetNumPositionals();
    size_t nextPositional = 2 + _iteratorFactory.GetNumArguments();
    bool checkSignature = nextPositional < numPositionals;
    bool onlyUnsigned = false;
    std::set<Offset> signatures;
    const SignatureDirectory<Offset>& signatureDirectory =
        _processImage->GetSignatureDirectory();

    if (checkSignature) {
      size_t signaturePositional = nextPositional++;
      if (nextPositional < numPositionals) {
        error << "Unexpected positional arguments found:\n";
        do {
          error << context.Positional(nextPositional++);
        } while (nextPositional < numPositionals);
        return;
      }
      const std::string& nameOrSignature =
          context.Positional(signaturePositional);
      if (nameOrSignature == "-") {
        onlyUnsigned = true;
      } else {
        signatures = signatureDirectory.Signatures(nameOrSignature);
        Offset signature;
	// Note that if a class has some name that happens to look OK
	// as hexadecimal, such as BEEF, for example, a requested
	// signature BEEF will be treated as referring to the class
	// name.  For purposes of pseudo signatures, the number can
	// be selected as a sudo-signature by prepending 0, or 0x
	// or anything that chap will parse as hexadecimal but that
	// will make it not match the symbol.
        if (signatures.empty() &&
	    context.ParsePositional(signaturePositional, signature)) {
          signatures.insert(signature);
        }
        if (signatures.empty()) {
          /*
           * A signature may not be recognized simply because there
           * were not any objects of the given time at the type the
           * process image was taken.  For now, even though it is
           * valid, we stop the command because nothing will be found.
           */
          error << "Signature \"" << nameOrSignature
                << "\" is not recognized.\n";
          return;
        }
      }
    }
    std::unique_ptr<Visitor> visitor;
    visitor.reset(_visitorFactory.MakeVisitor(context, *_processImage));
    if (visitor.get() == 0) {
      return;
    }

    Offset minSize = 0;
    Offset maxSize = ~((Offset)0);
    bool switchError = false;

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
        error << "Invalid size \"" << context.Argument("size", i) << "\"\n";
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
        error << "Invalid minsize \"" << context.Argument("minsize", i)
              << "\"\n";
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
        error << "Invalid maxsize \"" << context.Argument("maxsize", i)
              << "\"\n";
        switchError = true;
      } else {
        if (maxSize > size) {
          maxSize = size;
        }
      }
    }

    if (switchError) {
      return;
    }
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

    const VirtualAddressMap<Offset>& addressMap =
        _processImage->GetVirtualAddressMap();
    for (AllocationIndex index = iterator->Next(); index != numAllocations;
         index = iterator->Next()) {
      const Allocation* allocation = allocationFinder->AllocationAt(index);
      if (allocation == 0) {
        abort();
      }
      Offset size = allocation->Size();

      if (size < minSize || size > maxSize) {
        continue;
      }

      if (checkSignature) {
        const char* image;
        Offset numBytesFound =
            addressMap.FindMappedMemoryImage(allocation->Address(), &image);
        if (numBytesFound < size) {
          /*
           * This is not expected to happen on Linux but could, for
           * example, given null pages in the core.
           */
          output << "Note that allocation is not contiguously mapped.\n";
          size = numBytesFound;
        }

        if (onlyUnsigned) {
          if ((size >= sizeof(Offset)) &&
              signatureDirectory.IsMapped(*((Offset*)image))) {
            /*
             * The allocation was signed and only unsigned allocations
             * are wanted.
             */
            continue;
          }
        } else {
          if ((size < sizeof(Offset)) ||
              signatures.find(*((Offset*)image)) == signatures.end()) {
            /*
             * The signature was not matched.
             */
            continue;
          }
        }
      }
      visitor->Visit(index, *allocation);
    }
  }

  void ShowHelpMessage(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    _visitorFactory.ShowHelpMessage(context);
    output << "\n";
    _iteratorFactory.ShowHelpMessage(context);
    output << "\nThe set can be further restricted by appending a class"
              " name or any value\n"
              "in hexadecimal to match against the first "
           << std::dec << ((sizeof(Offset) * 8))
           << "-bit unsigned word, or by specifying\n\"-\" to accept"
              " only unsigned allocations.\n\n"
              "It can also be further restricted by any of the following:\n\n"
              "/minsize <size-in-hex> imposes a minimum size.\n"
              "/maxsize <size-in-hex> imposes a maximum size.\n"
              "/size <size-in-hex> imposes an exact size requirement.\n";
  }

 private:
  typename Visitor::Factory& _visitorFactory;
  typename Iterator::Factory& _iteratorFactory;
  const ProcessImage<Offset>* _processImage;
};
}  // namespace Subcommands
}  // namespace Allocations
}  // namespace chap
