// Copyright (c) 2017-2020,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../AnnotatorRegistry.h"
#include "../../Commands/Runner.h"
#include "../../Commands/SetBasedCommand.h"
#include "../../ProcessImage.h"
#include "../PatternDescriberRegistry.h"
#include "../SetCache.h"
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
      const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
      const AnnotatorRegistry<Offset>& annotatorRegistry,
      SetCache<Offset>& setCache)
      : _iteratorFactory(iteratorFactory),
        _countSubcommand(processImage, visitorFactories._counterFactory,
                         iteratorFactory, patternDescriberRegistry,
                         annotatorRegistry, setCache),
        _summarizeSubcommand(processImage, visitorFactories._summarizerFactory,
                             iteratorFactory, patternDescriberRegistry,
                             annotatorRegistry, setCache),
        _enumerateSubcommand(processImage, visitorFactories._enumeratorFactory,
                             iteratorFactory, patternDescriberRegistry,
                             annotatorRegistry, setCache),
        _listSubcommand(processImage, visitorFactories._listerFactory,
                        iteratorFactory, patternDescriberRegistry,
                        annotatorRegistry, setCache),
        _showSubcommand(processImage, visitorFactories._showerFactory,
                        iteratorFactory, patternDescriberRegistry,
                        annotatorRegistry, setCache),
        _describeSubcommand(processImage, visitorFactories._describerFactory,
                            iteratorFactory, patternDescriberRegistry,
                            annotatorRegistry, setCache),
        _explainSubcommand(processImage, visitorFactories._explainerFactory,
                           iteratorFactory, patternDescriberRegistry,
                           annotatorRegistry, setCache) {}

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
