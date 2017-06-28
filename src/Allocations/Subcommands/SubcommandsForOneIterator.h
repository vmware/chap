// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/SetBasedCommand.h"
#include "../../ProcessImage.h"
#include "../Visitors/Counter.h"
#include "../Visitors/Enumerator.h"
#include "../Visitors/Lister.h"
#include "../Visitors/Shower.h"
#include "../Visitors/Summarizer.h"
#include "../Visitors/Describer.h"
#include "../Visitors/Explainer.h"
#include "Subcommand.h"
namespace chap {
namespace Allocations {
namespace Subcommands {
template <class Offset, class Iterator>
class SubcommandsForOneIterator {
 public:
  SubcommandsForOneIterator(typename Iterator::Factory& iteratorFactory)
      : _iteratorFactory(iteratorFactory),
        _countSubcommand(_counterFactory, iteratorFactory),
        _summarizeSubcommand(_summarizerFactory, iteratorFactory),
        _enumerateSubcommand(_enumeratorFactory, iteratorFactory),
        _listSubcommand(_listerFactory, iteratorFactory),
        _showSubcommand(_showerFactory, iteratorFactory),
        _describeSubcommand(_describerFactory, iteratorFactory),
        _explainSubcommand(_explainerFactory, iteratorFactory),
        _processImage(0) {}

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    _countSubcommand.SetProcessImage(processImage);
    _summarizeSubcommand.SetProcessImage(processImage);
    _enumerateSubcommand.SetProcessImage(processImage);
    _listSubcommand.SetProcessImage(processImage);
    _showSubcommand.SetProcessImage(processImage);
    _describeSubcommand.SetProcessImage(processImage);
    _explainSubcommand.SetProcessImage(processImage);
  }
  void RegisterSubcommands(Commands::Runner& runner) {
    RegisterSubcommand(runner, _countSubcommand);
    RegisterSubcommand(runner, _summarizeSubcommand);
    RegisterSubcommand(runner, _enumerateSubcommand);
    RegisterSubcommand(runner, _listSubcommand);
    RegisterSubcommand(runner, _showSubcommand);
    RegisterSubcommand(runner, _describeSubcommand);
    RegisterSubcommand(runner, _explainSubcommand);
  }

 private:
  typename Iterator::Factory _iteratorFactory;

  typename Visitors::Counter<Offset>::Factory _counterFactory;
  Subcommands::Subcommand<Offset, typename Visitors::Counter<Offset>, Iterator>
      _countSubcommand;

  typename Visitors::Summarizer<Offset>::Factory _summarizerFactory;
  Subcommands::Subcommand<Offset, typename Visitors::Summarizer<Offset>,
                          Iterator>
      _summarizeSubcommand;

  typename Visitors::Enumerator<Offset>::Factory _enumeratorFactory;
  Subcommands::Subcommand<Offset, typename Visitors::Enumerator<Offset>,
                          Iterator>
      _enumerateSubcommand;

  typename Visitors::Lister<Offset>::Factory _listerFactory;
  Subcommands::Subcommand<Offset, typename Visitors::Lister<Offset>, Iterator>
      _listSubcommand;

  typename Visitors::Shower<Offset>::Factory _showerFactory;
  Subcommands::Subcommand<Offset, typename Visitors::Shower<Offset>, Iterator>
      _showSubcommand;

  typename Visitors::Describer<Offset>::Factory _describerFactory;
  Subcommands::Subcommand<Offset, typename Visitors::Describer<Offset>,
                          Iterator>
      _describeSubcommand;

  typename Visitors::Explainer<Offset>::Factory _explainerFactory;
  Subcommands::Subcommand<Offset, typename Visitors::Explainer<Offset>,
                          Iterator>
      _explainSubcommand;

  const ProcessImage<Offset>* _processImage;
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
};

}  // namespace Subcommands
}  // namespace Allocations
}  // namespace chap
