// Copyright (c) 2017 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include "../FileAnalyzerFactory.h"
#include "ELFCoreFileAnalyzer.h"

namespace chap {
namespace Linux {
class ELFCore32FileAnalyzerFactory : public FileAnalyzerFactory {
 public:
  ELFCore32FileAnalyzerFactory()
      : FileAnalyzerFactory("32-bit little-endian ELF core file") {}

  /*
   * Make a FileAnalyzer to analyze the supported file type on the
   * given file, returning 0 if the file is not of the correct type.
   */

  virtual FileAnalyzer* MakeFileAnalyzer(const FileImage& fileImage,
                                         bool truncationCheckOnly) {
    try {
      return new ELFCoreFileAnalyzer<Elf32>(fileImage, truncationCheckOnly);
    } catch (std::bad_alloc&) {
      std::cerr << "There is not enough memory on this server to process"
                   " this ELF file.\n";
      exit(1);
    } catch (...) {
    }
    return 0;
  }
};
}  // namespace Linux
}  // namespace chap
