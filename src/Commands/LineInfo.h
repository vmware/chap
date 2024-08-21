// Copyright (c) 2017 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
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
