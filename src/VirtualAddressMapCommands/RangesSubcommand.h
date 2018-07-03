// Copyright (c) 2018 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../PermissionsConstrainedRanges.h"
#include "../ProcessImage.h"
#include "../SizedTally.h"
namespace chap {
namespace VirtualAddressMapCommands {
template <class Offset>
class RangesSubcommand : public Commands::Subcommand {
 public:
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef const PermissionsConstrainedRanges<Offset>& (
      ProcessImage<Offset>::*RangesAccessor)() const;
  RangesSubcommand(const std::string& commandName,
                   const std::string& subcommandName,
                   const std::string& helpMessage,
                   RangesAccessor rangesAccessor)
      : Commands::Subcommand(commandName, subcommandName),
        _helpMessage(helpMessage),
        _rangesAccessor(rangesAccessor),
        _processImage(0) {}

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    if (processImage != NULL) {
      _ranges = &((processImage->*_rangesAccessor)());
    } else {
      _ranges = (const PermissionsConstrainedRanges<Offset>*)(0);
    }
  }

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput() << _helpMessage;
  }

  virtual void VisitRanges(Commands::Context&) = 0;

  void Run(Commands::Context& context) {
    Commands::Error& error = context.GetError();
    Commands::Output& output = context.GetOutput();
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
    VisitRanges(context);
  }

 private:
  const std::string _helpMessage;
  RangesAccessor _rangesAccessor;

 protected:
  const ProcessImage<Offset>* _processImage;
  const PermissionsConstrainedRanges<Offset>* _ranges;
};
}  // namespace VirtualAddressMapCommands
}  // namespace chap
