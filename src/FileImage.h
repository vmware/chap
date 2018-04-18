// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
extern "C" {
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
};
namespace chap {
class FileImage {
 public:
  FileImage(const char *filePath, bool verboseOnFailure = true)
      : _filePath(filePath),
        _fileSize(0)

  {
    _fd = open(filePath, O_RDONLY);
    if (_fd < 0) {
      if (verboseOnFailure) {
        std::cerr << "Cannot open file \"" << filePath << "\" for reading.\n";
        char *openFailCause = strerror(errno);
        if (openFailCause) {
          std::cerr << openFailCause << "\n";
        }
      }
      throw "cannot open file";
    }

    struct stat statBuf;
    if (fstat(_fd, &statBuf) == 0) {
      if (!S_ISREG(statBuf.st_mode)) {
        if (verboseOnFailure) {
          if (S_ISDIR(statBuf.st_mode)) {
            std::cerr << "\"" << _filePath << "\" is a directory." << std::endl;
          } else {
            std::cerr << "\"" << _filePath << "\" is not a regular file."
                      << std::endl;
          }
        }
        close(_fd);
        throw "not a regular file";
      }
    } else {
      if (verboseOnFailure) {
        std::cerr << "An fstat call failed on \"" << _filePath << "\"."
                  << std::endl;
      }
      close(_fd);
      throw "fstat failed";
    }
    off64_t fileSize = lseek64(_fd, (off64_t)0, SEEK_END);
    if (fileSize < 0) {
      if (verboseOnFailure) {
        std::cerr << "Failed to determine size of file " << _filePath
                  << std::endl;
      }
      close(_fd);
      throw "failed to determine size of file";
    }
    _fileSize = (uint64_t)fileSize;

    if (_fileSize == 0) {
      if (verboseOnFailure) {
        std::cerr << "File " << _filePath << " is empty." << std::endl;
      }
      close(_fd);
      throw "empty file";
    }
    _image = (char *)mmap((void *)0, (size_t)fileSize, PROT_READ, MAP_PRIVATE,
                          _fd, (off_t)0);
    if (_image == (char *)(-1)) {
      if (verboseOnFailure) {
        std::cerr << "mmap() failed with errno " << std::dec << errno
                  << std::endl;
        if (errno == ENOMEM) {
          std::cerr
              << "You should try on a Linux server that has more memory.\n";
        }
      }
      close(_fd);
      throw "mmap failed";
    }
  }
  ~FileImage() {
    (void)munmap(_image, _fileSize);
    if (_fd >= 0) {
      close(_fd);
    }
  }
  int _fd;
  const char *GetImage() const { return _image; }
  uint64_t GetFileSize() const { return _fileSize; }
  const std::string &GetFileName() const { return _filePath; }

 private:
  std::string _filePath;
  uint64_t _fileSize;
  char *_image;
};
}  // namespace chap
