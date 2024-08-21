// Copyright (c) 2023 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../FileImage.h"
#include "../ModuleImage.h"
#include "ELFImage.h"

namespace chap {
namespace Linux {
template <class ElfImage>
class ELFModuleImage : public ModuleImage<typename ElfImage::Offset> {
 public:
  ELFModuleImage(const std::string& filePath)
      : _fileImage(filePath.c_str(), false), _elfImage(_fileImage) {
    uint16_t elfType = _elfImage.GetELFType();
    if (elfType != ET_EXEC && elfType != ET_DYN) {
      std::cerr << "Warning: there was an attempt to reference " << filePath
                << " as a shared library or executable.\n";
      throw filePath;
    }
  }
  ~ELFModuleImage() {}
  const VirtualAddressMap<typename ElfImage::Offset>& GetVirtualAddressMap()
      const {
    return _elfImage.GetVirtualAddressMap();
  }
  const FileImage& GetFileImage() const { return _fileImage; }
  const std::string& GetPath() const { return _fileImage.GetFileName(); }

 private:
  FileImage _fileImage;
  ElfImage _elfImage;
};
}  // namespace Linux
}  // namespace chap
