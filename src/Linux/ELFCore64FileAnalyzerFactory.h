// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include "../FileAnalyzerFactory.h"
#include "ELFCoreFileAnalyzer.h"

namespace chap {
namespace Linux {
class ELFCore64FileAnalyzerFactory : public FileAnalyzerFactory {
 public:
  ELFCore64FileAnalyzerFactory()
      : FileAnalyzerFactory("64-bit little-endian ELF core file") {}

  /*
   * Make a FileAnalyzer to analyze the supported file type on the
   * given file, returning 0 if the file is not of the correct type.
   */

  virtual FileAnalyzer* MakeFileAnalyzer(const FileImage& fileImage) {
    try {
      return new ELFCoreFileAnalyzer<Elf64>(fileImage);
    } catch (...) {
      return 0;
    }
  }
};
}  // namespace Linux
}  // namespace chap
