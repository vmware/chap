// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Allocations/Describer.h"
#include "Allocations/PatternRecognizerRegistry.h"
#include "Allocations/Subcommands/DefaultSubcommands.h"
#include "Allocations/Subcommands/SummarizeSignatures.h"
#include "Commands/CountCommand.h"
#include "Commands/DescribeCommand.h"
#include "Commands/EnumerateCommand.h"
#include "Commands/ExplainCommand.h"
#include "Commands/ListCommand.h"
#include "Commands/Runner.h"
#include "Commands/ShowCommand.h"
#include "Commands/SummarizeCommand.h"
#include "CompoundDescriber.h"
#include "InModuleDescriber.h"
#include "KnownAddressDescriber.h"
#include "ModuleCommands/ListModules.h"
#include "ProcessImage.h"
#include "StackDescriber.h"
#include "ThreadMapCommands/CountStacks.h"
#include "ThreadMapCommands/ListStacks.h"

namespace chap {
template <typename Offset>
class ProcessImageCommandHandler {
 public:
  typedef ProcessImageCommandHandler<Offset> ThisClass;
  ProcessImageCommandHandler(const ProcessImage<Offset>* processImage)
      : _stackDescriber(0),
        _patternRecognizerRegistry(processImage),
        _allocationDescriber(_inModuleDescriber, _stackDescriber,
                             _patternRecognizerRegistry, 0),
        _knownAddressDescriber(0),
        _inModuleDescriber(0, _knownAddressDescriber),
        _describeCommand(_compoundDescriber),
        _explainCommand(_compoundDescriber),
        _defaultAllocationsSubcommands(_allocationDescriber,
                                       _patternRecognizerRegistry) {
    SetProcessImage(processImage);
    _compoundDescriber.AddDescriber(&_allocationDescriber);
    _compoundDescriber.AddDescriber(&_stackDescriber);
    _compoundDescriber.AddDescriber(&_inModuleDescriber);
    /*
     * The following should alway be added last because describers are
     * checked in the order given and the first applicable describer applies.
     */
    _compoundDescriber.AddDescriber(&_knownAddressDescriber);
  }

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    _defaultAllocationsSubcommands.SetProcessImage(processImage);
    _patternRecognizerRegistry.SetProcessImage(processImage);
    _allocationDescriber.SetProcessImage(processImage);
    _stackDescriber.SetProcessImage(processImage);
    _knownAddressDescriber.SetProcessImage(processImage);
    _inModuleDescriber.SetProcessImage(processImage);
    _countStacksSubcommand.SetProcessImage(processImage);
    _listStacksSubcommand.SetProcessImage(processImage);
    _listModulesSubcommand.SetProcessImage(processImage);
    _summarizeSignaturesSubcommand.SetProcessImage(processImage);
  }

  virtual void AddCommandCallbacks(Commands::Runner& /* r */) {}

  virtual void AddCommands(Commands::Runner& r) {
    r.AddCommand(_countCommand);
    r.AddCommand(_summarizeCommand);
    r.AddCommand(_enumerateCommand);
    r.AddCommand(_listCommand);
    r.AddCommand(_showCommand);
    r.AddCommand(_describeCommand);
    r.AddCommand(_explainCommand);
    RegisterSubcommand(r, _countStacksSubcommand);
    RegisterSubcommand(r, _listStacksSubcommand);
    RegisterSubcommand(r, _listModulesSubcommand);
    RegisterSubcommand(r, _summarizeSignaturesSubcommand);
    _defaultAllocationsSubcommands.RegisterSubcommands(r);
  }

 protected:
  const ProcessImage<Offset>* _processImage;
  StackDescriber<Offset> _stackDescriber;
  Allocations::PatternRecognizerRegistry<Offset> _patternRecognizerRegistry;
  Allocations::Describer<Offset> _allocationDescriber;
  KnownAddressDescriber<Offset> _knownAddressDescriber;
  InModuleDescriber<Offset> _inModuleDescriber;
  CompoundDescriber<Offset> _compoundDescriber;
  Commands::CountCommand _countCommand;
  Commands::SummarizeCommand _summarizeCommand;
  Commands::EnumerateCommand _enumerateCommand;
  Commands::ListCommand _listCommand;
  Commands::ShowCommand _showCommand;
  Commands::DescribeCommand<Offset> _describeCommand;
  Commands::ExplainCommand<Offset> _explainCommand;
  ThreadMapCommands::CountStacks<Offset> _countStacksSubcommand;
  ThreadMapCommands::ListStacks<Offset> _listStacksSubcommand;
  ModuleCommands::ListModules<Offset> _listModulesSubcommand;
  Allocations::Subcommands::SummarizeSignatures<Offset>
      _summarizeSignaturesSubcommand;

  void RegisterSubcommand(Commands::Runner& runner,
                          Commands::Subcommand& subcommand) {
    const std::string& commandName = subcommand.GetCommandName();
    const std::string& setName = subcommand.GetSetName();
    Commands::Command* command = runner.FindCommand(commandName);
    if (command == 0) {
      std::cerr << "Attempted to register subcommand \"" << commandName << " "
                << setName << "\" for command that does\nnot exist.\n";
      return;
    }
    Commands::SetBasedCommand* setBasedCommand =
        dynamic_cast<typename Commands::SetBasedCommand*>(command);
    if (setBasedCommand == 0) {
      std::cerr << "Attempted to register subcommand \"" << commandName << " "
                << setName << " for command that is\nnot set based.\n";
      return;
    }
    setBasedCommand->AddSubcommand(subcommand);
  }

 private:
  Allocations::Subcommands::DefaultSubcommands<Offset>
      _defaultAllocationsSubcommands;
};

}  // namespace chap
