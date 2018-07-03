// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
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
    ThreadInfo()
        : _stackBase(0), _stackPointer(0), _stackLimit(0), _registers(0){};
    Offset _stackBase;
    Offset _stackPointer;
    Offset _stackLimit;
    Offset *_registers;
    size_t _threadNum;
  };
  void AddThread(Offset stackBase, Offset stackPointer, Offset stackLimit,
                 Offset *registers, size_t threadNum) {
    _threads.push_back(ThreadInfo());
    ThreadInfo &threadInfo = _threads.back();
    threadInfo._stackBase = stackBase;
    threadInfo._stackPointer = stackPointer;
    threadInfo._stackLimit = stackLimit;
    threadInfo._registers = registers;
    threadInfo._threadNum = threadNum;
    _stackRanges.MapRange(stackBase, stackLimit - stackBase,
                          &threadInfo - &(*_threads.begin()));
  }
  typedef typename std::vector<ThreadInfo>::const_iterator const_iterator;
  const_iterator begin() const { return _threads.begin(); }
  const_iterator end() const { return _threads.end(); }
  size_t NumThreads() const { return _threads.size(); }
  const ThreadInfo *find(Offset stackAddress) const {
    const ThreadInfo *pThreadInfo = (ThreadInfo *)(0);
    typename RangeMapper<Offset, size_t>::const_iterator itStack =
        _stackRanges.find(stackAddress);
    if (itStack != _stackRanges.end()) {
      pThreadInfo = &(_threads[itStack->_value]);
    }
    return pThreadInfo;
  }
  class ThreadInfoByStackConstIterator {
   public:
    ThreadInfoByStackConstIterator(
        typename RangeMapper<Offset, size_t>::const_iterator iterator,
        const ThreadInfo *firstThreadInfo)
        : _iterator(iterator), _firstThreadInfo(firstThreadInfo) {}
    bool operator==(const ThreadInfoByStackConstIterator &other) {
      return this->_iterator == other._iterator;
    }
    bool operator!=(const ThreadInfoByStackConstIterator &other) {
      return this->_iterator != other._iterator;
    }
    ThreadInfoByStackConstIterator &operator++() {
      ++_iterator;
      return *this;
    }
    const ThreadInfo &operator*() {
      return *(_firstThreadInfo + _iterator->_value);
    }
    const ThreadInfo *operator->() {
      return _firstThreadInfo + _iterator->_value;
    }

   private:
    typename RangeMapper<Offset, size_t>::const_iterator _iterator;
    const ThreadInfo *_firstThreadInfo;
  };
  ThreadInfoByStackConstIterator ThreadInfoByStackBegin() const {
    return ThreadInfoByStackConstIterator(_stackRanges.begin(),
                                          &(*_threads.begin()));
  }
  ThreadInfoByStackConstIterator ThreadInfoByStackEnd() const {
    return ThreadInfoByStackConstIterator(_stackRanges.end(),
                                          &(*_threads.begin()));
  }

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
  RangeMapper<Offset, size_t> _stackRanges;
};
}  // namespace chap
