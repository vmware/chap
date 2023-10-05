// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include "ModuleImage.h"

namespace chap {
template <typename Offset>
class ModuleImageFactory {
 public:
  virtual ModuleImage<Offset>* MakeModuleImage(const std::string& filePath) = 0;
};
}  // namespace chap
