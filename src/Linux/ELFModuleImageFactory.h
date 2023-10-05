// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "../ModuleImageFactory.h"
#include "ELFModuleImage.h"

namespace chap {
namespace Linux {
template <class ElfImage>
class ELFModuleImageFactory
    : public ModuleImageFactory<typename ElfImage::Offset> {
 public:
  ELFModuleImageFactory() {}
  virtual ModuleImage<typename ElfImage::Offset>* MakeModuleImage(
      const std::string& filePath) {
    try {
      return new ELFModuleImage<ElfImage>(filePath);
    } catch (...) {
      return nullptr;
    }
  }
};
}  // namespace Linux
}  // namespace chap
