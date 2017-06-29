// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <string>
#include "FileAnalyzer.h"
#include "FileImage.h"

namespace chap {
class FileAnalyzerFactory {
 public:
  FileAnalyzerFactory(const std::string& supportedFileFormat)
      : _supportedFileFormat(supportedFileFormat) {}

  /*
   * Return a brief text description the supported file format.  This
   * should never throw.
   */

  virtual const std::string& GetSupportedFileFormat() {
    return _supportedFileFormat;
  }

  /*
   * Make a FileAnalyzer to analyze the supported file type on the
   * given file, returning NULL if  the format is not supported.
   */

  virtual FileAnalyzer* MakeFileAnalyzer(const FileImage& fileImage,
                                         bool truncationCheckOnly) = 0;

 protected:
  const std::string _supportedFileFormat;
};
}  // namespace chap
