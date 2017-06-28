// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Allocations/Subcommands/DefaultSubcommands.h"
#include "Commands/CountCommand.h"
#include "Commands/EnumerateCommand.h"
#include "Commands/ListCommand.h"
#include "Commands/Runner.h"
#include "Commands/ShowCommand.h"
#include "Commands/SummarizeCommand.h"
#include "Commands/DescribeCommand.h"
#include "Commands/ExplainCommand.h"
#include "Commands/Runner.h"
#include "Allocations/Describer.h"
#include "CompoundDescriber.h"
#include "InModuleDescriber.h"
#include "StackDescriber.h"
#include "ProcessImage.h"
#include "ThreadMapCommandHandler.h"

namespace chap {
template <typename Offset>
class ProcessImageCommandHandler {
 public:
  typedef ProcessImageCommandHandler<Offset> ThisClass;
  ProcessImageCommandHandler(const ProcessImage<Offset> *processImage)
      : _stackDescriber(0),
        _inModuleDescriber(0),
        _allocationDescriber(_inModuleDescriber, _stackDescriber, 0),
        _describeCommand(_compoundDescriber),
        _explainCommand(_compoundDescriber),
        _threadMapCommandHandler(0) {
    SetProcessImage(processImage);
    _compoundDescriber.AddDescriber(&_allocationDescriber);
    _compoundDescriber.AddDescriber(&_stackDescriber);
    _compoundDescriber.AddDescriber(&_inModuleDescriber);
  }

  void SetProcessImage(const ProcessImage<Offset> *processImage) {
    _processImage = processImage;
    if (processImage != NULL) {
      _threadMapCommandHandler.SetThreadMap(&processImage->GetThreadMap());
    } else {
      _threadMapCommandHandler.SetThreadMap((const ThreadMap<Offset> *)(0));
    }
    _defaultAllocationsSubcommands.SetProcessImage(processImage);
    _allocationDescriber.SetProcessImage(processImage);
    _stackDescriber.SetProcessImage(processImage);
    _inModuleDescriber.SetProcessImage(processImage);
  }

  virtual void AddCommandCallbacks(Commands::Runner &r) {
    _threadMapCommandHandler.AddCommandCallbacks(r);
  }

  virtual void AddCommands(Commands::Runner &r) {
    r.AddCommand(_countCommand);
    r.AddCommand(_summarizeCommand);
    r.AddCommand(_enumerateCommand);
    r.AddCommand(_listCommand);
    r.AddCommand(_showCommand);
    r.AddCommand(_describeCommand);
    r.AddCommand(_explainCommand);
    _defaultAllocationsSubcommands.RegisterSubcommands(r);
  }

 protected:
  const ProcessImage<Offset> *_processImage;
  StackDescriber<Offset> _stackDescriber;
  InModuleDescriber<Offset> _inModuleDescriber;
  Allocations::Describer<Offset> _allocationDescriber;
  CompoundDescriber<Offset> _compoundDescriber;
  Commands::CountCommand _countCommand;
  Commands::SummarizeCommand _summarizeCommand;
  Commands::EnumerateCommand _enumerateCommand;
  Commands::ListCommand _listCommand;
  Commands::ShowCommand _showCommand;
  Commands::DescribeCommand<Offset> _describeCommand;
  Commands::ExplainCommand<Offset> _explainCommand;

 private:
  ThreadMapCommandHandler<Offset> _threadMapCommandHandler;
  Allocations::Subcommands::DefaultSubcommands<Offset>
      _defaultAllocationsSubcommands;
};

}  // namespace chap
