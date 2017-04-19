// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
namespace chap {
namespace Commands {
struct LineInfo {
  LineInfo(std::string path, size_t line) : _path(path), _line(line) {}
  LineInfo(const LineInfo& other) : _path(other._path), _line(other._line) {}

  std::string _path;
  size_t _line;
};
}  // namespace Commands
}  // namespace chap
