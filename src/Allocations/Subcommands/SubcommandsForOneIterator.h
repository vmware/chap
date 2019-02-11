// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/SetBasedCommand.h"
#include "../../ProcessImage.h"
#include "../PatternRecognizerRegistry.h"
#include "../Visitors/DefaultVisitorFactories.h"
#include "Subcommand.h"
namespace chap {
namespace Allocations {
namespace Subcommands {
template <class Offset, class Iterator>
class SubcommandsForOneIterator {
 public:
  SubcommandsForOneIterator(
      const ProcessImage<Offset>& processImage,
      typename Iterator::Factory& iteratorFactory,
      typename Visitors::DefaultVisitorFactories<Offset>& visitorFactories,
      const PatternRecognizerRegistry<Offset>& patternRecognizerRegistry)
      : _iteratorFactory(iteratorFactory),
        _countSubcommand(processImage, visitorFactories._counterFactory,
                         iteratorFactory, patternRecognizerRegistry),
        _summarizeSubcommand(processImage, visitorFactories._summarizerFactory,
                             iteratorFactory, patternRecognizerRegistry),
        _enumerateSubcommand(processImage, visitorFactories._enumeratorFactory,
                             iteratorFactory, patternRecognizerRegistry),
        _listSubcommand(processImage, visitorFactories._listerFactory,
                        iteratorFactory, patternRecognizerRegistry),
        _showSubcommand(processImage, visitorFactories._showerFactory,
                        iteratorFactory, patternRecognizerRegistry),
        _describeSubcommand(processImage, visitorFactories._describerFactory,
                            iteratorFactory, patternRecognizerRegistry),
        _explainSubcommand(processImage, visitorFactories._explainerFactory,
                           iteratorFactory, patternRecognizerRegistry) {}

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

  Subcommands::Subcommand<Offset, typename Visitors::Counter<Offset>, Iterator>
      _countSubcommand;
  Subcommands::Subcommand<Offset, typename Visitors::Summarizer<Offset>,
                          Iterator>
      _summarizeSubcommand;
  Subcommands::Subcommand<Offset, typename Visitors::Enumerator<Offset>,
                          Iterator>
      _enumerateSubcommand;
  Subcommands::Subcommand<Offset, typename Visitors::Lister<Offset>, Iterator>
      _listSubcommand;
  Subcommands::Subcommand<Offset, typename Visitors::Shower<Offset>, Iterator>
      _showSubcommand;
  Subcommands::Subcommand<Offset, typename Visitors::Describer<Offset>,
                          Iterator>
      _describeSubcommand;
  Subcommands::Subcommand<Offset, typename Visitors::Explainer<Offset>,
                          Iterator>
      _explainSubcommand;

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
