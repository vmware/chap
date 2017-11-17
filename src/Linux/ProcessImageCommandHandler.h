// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ProcessImageCommandHandler.h"
#include "COWStringBodyRecognizer.h"

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
  }

  virtual void AddCommands(Commands::Runner &r) {
    chap::ProcessImageCommandHandler<Offset>::AddCommands(r);
  }

};

}  // namespace Linux
}  // namespace chap
