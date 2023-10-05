// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
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
  ELFModuleImage(const std::string& filePath) {
    _fileImage.reset(new FileImage(filePath.c_str(), false));
    _elfImage.reset(new ElfImage(*_fileImage));
    uint16_t elfType = _elfImage->GetELFType();
    if (elfType != ET_EXEC && elfType != ET_DYN) {
      std::cerr << "Warning: there was an attempt to reference " << filePath
                << " as a shared library or executable.\n";
      throw filePath;
    }
  }
  const VirtualAddressMap<typename ElfImage::Offset>& GetVirtualAddressMap()
      const {
    return _elfImage->GetVirtualAddressMap();
  }

 private:
  std::unique_ptr<FileImage> _fileImage;
  std::unique_ptr<ElfImage> _elfImage;
};
}  // namespace Linux
}  // namespace chap
