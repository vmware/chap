// Copyright (c) 2017,2021,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "RangeMapper.h"
namespace chap {
template <typename OffsetType>
class ThreadMap {
 public:
  typedef OffsetType Offset;
  ThreadMap(const char *const *registerNames, size_t numRegisters)
      : _registerNames(registerNames), _numRegisters(numRegisters) {}

  struct ThreadInfo {
    ThreadInfo() : _registers(0){};
    ThreadInfo(Offset *registers, size_t threadNum, Offset stackPointer)
        : _registers(registers),
          _threadNum(threadNum),
          _stackPointer(stackPointer) {}
    Offset *_registers;
    size_t _threadNum;
    Offset _stackPointer;
  };
  void AddThread(Offset *registers, size_t threadNum, Offset stackPointer) {
    _threads.emplace_back(registers, threadNum, stackPointer);
  }
  typedef typename std::vector<ThreadInfo>::const_iterator const_iterator;
  const_iterator begin() const { return _threads.begin(); }
  const_iterator end() const { return _threads.end(); }
  size_t NumThreads() const { return _threads.size(); }

  size_t GetNumRegisters() const { return _numRegisters; }
  const char *GetRegisterName(size_t registerNumber) const {
    if (registerNumber < _numRegisters) {
      return _registerNames[registerNumber];
    } else {
      return "???";
    }
  }

 private:
  const char *const *_registerNames;
  const size_t _numRegisters;
  std::vector<ThreadInfo> _threads;
};
}  // namespace chap
