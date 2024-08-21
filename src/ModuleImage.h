// Copyright (c) 2023 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "VirtualAddressMap.h"

namespace chap {
template <typename Offset>
class ModuleImage {
 public:
  virtual ~ModuleImage(){};
  const virtual VirtualAddressMap<Offset>& GetVirtualAddressMap() const = 0;
  const virtual FileImage& GetFileImage() const = 0;
  const virtual std::string& GetPath() const = 0;
};
}  // namespace chap
