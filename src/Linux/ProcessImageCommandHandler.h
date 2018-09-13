// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ProcessImageCommandHandler.h"
#include "COWStringBodyRecognizer.h"
#include "LongStringRecognizer.h"
#include "SSLRecognizer.h"
#include "SSL_CTXRecognizer.h"
#include "PyDictKeysObjectRecognizer.h"
#include "Subcommands/DescribeArenas.h"

namespace chap {
namespace Linux {
template <typename Offset>
class ProcessImageCommandHandler
    : public chap::ProcessImageCommandHandler<Offset> {
 public:
  ProcessImageCommandHandler(const ProcessImage<Offset> *processImage)
      : chap::ProcessImageCommandHandler<Offset>(processImage) {
    chap::ProcessImageCommandHandler<Offset>::_patternRecognizerRegistry
        .Register(new COWStringBodyRecognizer<Offset>(processImage));
    chap::ProcessImageCommandHandler<Offset>::_patternRecognizerRegistry
        .Register(new LongStringRecognizer<Offset>(processImage));
    chap::ProcessImageCommandHandler<Offset>::_patternRecognizerRegistry
        .Register(new SSL_CTXRecognizer<Offset>(processImage));
    chap::ProcessImageCommandHandler<Offset>::_patternRecognizerRegistry
        .Register(new SSLRecognizer<Offset>(processImage));
    chap::ProcessImageCommandHandler<Offset>::_patternRecognizerRegistry
        .Register(new PyDictKeysObjectRecognizer<Offset>(processImage));
    SetProcessImage(processImage);
  }

  void SetProcessImage(const ProcessImage<Offset> *processImage) {
    _describeArenasSubcommand.SetProcessImage(processImage);
  }

  virtual void AddCommands(Commands::Runner &r) {
    chap::ProcessImageCommandHandler<Offset>::AddCommands(r);
    chap::ProcessImageCommandHandler<Offset>::RegisterSubcommand(
        r, _describeArenasSubcommand);
  }

 private:
  Subcommands::DescribeArenas<Offset> _describeArenasSubcommand;
};

}  // namespace Linux
}  // namespace chap
