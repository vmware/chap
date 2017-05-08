// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "AllocationExplainer.h"
#include "Allocations/Subcommands/DefaultSubcommands.h"
#include "Commands/Runner.h"
#include "InModuleExplainer.h"
#include "ProcessImage.h"
#include "StackExplainer.h"
#include "ThreadMapCommandHandler.h"

namespace chap {
template <typename Offset>
class ProcessImageCommandHandler {
 public:
  typedef ProcessImageCommandHandler<Offset> ThisClass;
  ProcessImageCommandHandler(const ProcessImage<Offset> *processImage)
      : _processImage(processImage),
        _threadMapCommandHandler(
            (processImage != NULL) ? (&processImage->GetThreadMap()) : NULL),
        _allocationExplainer(_inModuleExplainer, _stackExplainer,
                             (processImage != NULL)
                                 ? (&processImage->GetSignatureDirectory())
                                 : (const SignatureDirectory<Offset> *)(0)) {
    _defaultAllocationsSubcommands.SetProcessImage(processImage);
  }

  void SetProcessImage(const ProcessImage<Offset> *processImage) {
    _processImage = processImage;
    if (processImage != NULL) {
      _threadMapCommandHandler.SetThreadMap(&processImage->GetThreadMap());
      _allocationExplainer.SetSignatureDirectory(
          &(_processImage->GetSignatureDirectory()));
      _inModuleExplainer.SetModuleDirectory(
          &(_processImage->GetModuleDirectory()));
      _stackExplainer.SetThreadMap(&(_processImage->GetThreadMap()));
    } else {
      _threadMapCommandHandler.SetThreadMap((const ThreadMap<Offset> *)(0));
      _allocationExplainer.SetSignatureDirectory(
          (const SignatureDirectory<Offset> *)(0));
      _inModuleExplainer.SetModuleDirectory(
          (const ModuleDirectory<Offset> *)(0));
      _stackExplainer.SetThreadMap((const ThreadMap<Offset> *)(0));
    }
    _defaultAllocationsSubcommands.SetProcessImage(processImage);
  }

  size_t Explain(Commands::Context &context, bool checkOnly) {
    Commands::Output &output = context.GetOutput();
    Offset addrToExplain;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "explain") {
      numTokensAccepted++;
      if (context.ParseTokenAt(1, addrToExplain)) {
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error &error = context.GetError();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 2) {
        error << "Usage: explain <addr-in-hex>\n";
      } else {
        if (_processImage == NULL) {
          error << "No process image could be obtained from the dump.\n";
          error << "The \"explain\" command won't work without that.\n";
        } else {
          // The allocation graph is allocated lazily, because it is
          // time consuming to set it up.
          _allocationExplainer.SetAllocationGraph(
              _processImage->GetAllocationGraph());
          if (!_allocationExplainer.Explain(context, addrToExplain) &&
              !_inModuleExplainer.Explain(context, addrToExplain) &&
              !_stackExplainer.Explain(context, addrToExplain)) {
            output << "No explanation is available yet.\n";
          }
        }
      }
    }
    return numTokensAccepted;
  }

  virtual void AddCommandCallbacks(Commands::Runner &r) {
    _defaultAllocationsSubcommands.RegisterSubcommands(r);
    _threadMapCommandHandler.AddCommandCallbacks(r);
    r.AddCommand("explain",
                 std::bind(&ThisClass::Explain, this, std::placeholders::_1,
                           std::placeholders::_2));
  }

 private:
  const ProcessImage<Offset> *_processImage;
  ThreadMapCommandHandler<Offset> _threadMapCommandHandler;
  InModuleExplainer<Offset> _inModuleExplainer;
  StackExplainer<Offset> _stackExplainer;
  AllocationExplainer<Offset> _allocationExplainer;
  Allocations::Subcommands::DefaultSubcommands<Offset>
      _defaultAllocationsSubcommands;
};

}  // namespace chap
