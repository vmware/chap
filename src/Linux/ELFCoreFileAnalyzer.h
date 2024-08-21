// Copyright (c) 2017-2019 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../FileAnalyzer.h"
#include "../VirtualAddressMapCommandHandler.h"
#include "ELFImage.h"
#include "LinuxProcessImage.h"
#include "ProcessImageCommandHandler.h"

namespace chap {
namespace Linux {
template <class ElfImage>
class ELFCoreFileAnalyzer : public FileAnalyzer {
 public:
  typedef typename ElfImage::Offset Offset;
  ELFCoreFileAnalyzer(const FileImage& fileImage, bool truncationCheckOnly)
      : _elfImage(fileImage),
        _virtualAddressMap(_elfImage.GetVirtualAddressMap()),
        _virtualAddressMapCommandHandler(_virtualAddressMap) {
    if (_elfImage.GetELFType() == ET_CORE) {
      _processImage.reset(
          new LinuxProcessImage<ElfImage>(_elfImage, truncationCheckOnly));
      if (!truncationCheckOnly) {
        _processImageCommandHandler.reset(
            new ProcessImageCommandHandler<ElfImage>(*(_processImage.get())));
      }
    } else {
      std::cerr << "This image is an ELF file but not an ELF core.\n";
      std::cerr
          << "Commands related to process images will not be available.\n";
    }
  }

  /*
   * Return true if the file is known to be truncated.
   */

  virtual bool FileIsKnownTruncated() { return _elfImage.IsTruncated(); }

  /*
   * Return the actual file size of the current file or 0 if none.  This
   * should never throw.
   */

  virtual uint64_t GetFileSize() const { return _elfImage.GetFileSize(); }

  /*
   * Return the expected file size, based on information from the start of
   * the file, or 0 if the expected file size is not known.  Note that if the
   * file format is unsupported, the expected file normally will not be known
   * but this is not a requirement, because, for example, an ELF crash dump
   * anaylyzer would not support an ELF executable but still might reasonably
   * be able to determine the expected size as part of common ELF processing.
   */

  virtual uint64_t GetMinimumExpectedFileSize() const {
    return _elfImage.GetMinimumExpectedFileSize();
  }

  /*
   * Add command runners.  This should include all the ones reasonably
   * supported for this file format, including ones that are disabled because
   * some key piece of information is missing from the file.
   */

  virtual void AddCommandCallbacks(Commands::Runner& r) {
    // TODO: support raw operations on the file by offset
    // FileAnalyzer::AddCommandCallbacks(r);
    _virtualAddressMapCommandHandler.AddCommandCallbacks(r);
    if (_processImageCommandHandler.get() != 0) {
      _processImageCommandHandler->AddCommandCallbacks(r);
    }
  }

  virtual void AddCommands(Commands::Runner& r) {
    if (_processImageCommandHandler.get() != 0) {
      _processImageCommandHandler->AddCommands(r);
    }
    r.SetPreCommandCallback([this]() {
      this->_processImage->RefreshSignaturesAndAnchors();
    });
  }

 private:
  ElfImage _elfImage;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  VirtualAddressMapCommandHandler<Offset> _virtualAddressMapCommandHandler;
  std::unique_ptr<LinuxProcessImage<ElfImage> > _processImage;
  std::unique_ptr<ProcessImageCommandHandler<ElfImage> >
      _processImageCommandHandler;
};
}  // namespace Linux
}  // namespace chap
