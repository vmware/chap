// Copyright (c) 2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <vector>
#include "RangeMapper.h"

namespace chap {
template <typename Offset>
class StackRegistry {
 public:
  static constexpr Offset STACK_TOP_UNKNOWN = ~0;
  static constexpr Offset STACK_BASE_UNKNOWN = ~0;
  static constexpr size_t THREAD_NUMBER_UNKNOWN = ~0;
  StackRegistry() : _stacks(false) {}

  bool RegisterStack(Offset regionBase, Offset regionLimit,
                     const char *stackType) {
    if (_stacks.MapRange(regionBase, regionLimit - regionBase,
                         _stackInfo.size())) {
      _stackInfo.emplace_back(stackType);
      return true;
    }
    return false;
  }

  bool AddStackTop(Offset stackTop) {
    Offset regionBase;
    Offset regionSize;
    size_t stackInfoIndex;
    if (_stacks.FindRange(stackTop, regionBase, regionSize, stackInfoIndex)) {
      _stackInfo[stackInfoIndex]._stackTop = stackTop;
      return true;
    }
    return false;
  }

  bool AddStackBase(Offset stackBase) {
    Offset regionBase;
    Offset regionSize;
    size_t stackInfoIndex;
    if (_stacks.FindRange(stackBase, regionBase, regionSize, stackInfoIndex)) {
      _stackInfo[stackInfoIndex]._stackBase = stackBase;
      return true;
    }
    return false;
  }

  bool AddThreadNumber(Offset stackTop, size_t threadNumber) {
    Offset regionBase;
    Offset regionSize;
    size_t stackInfoIndex;
    if (_stacks.FindRange(stackTop, regionBase, regionSize, stackInfoIndex)) {
      _stackInfo[stackInfoIndex]._threadNumber = threadNumber;
      _stackInfo[stackInfoIndex]._stackTop = stackTop;
      return true;
    }
    return false;
  }

  template <typename Visitor>
  void VisitStacks(Visitor visitor) const {
    typename RangeMapper<Offset, size_t>::const_iterator itEnd = _stacks.end();
    for (typename RangeMapper<Offset, size_t>::const_iterator it =
             _stacks.begin();
         it != itEnd; ++it) {
      const StackInfo &stackInfo = _stackInfo[it->_value];
      if (!visitor(it->_base, it->_limit, stackInfo._stackType,
                   stackInfo._stackTop, stackInfo._stackBase,
                   stackInfo._threadNumber)) {
        break;
      }
    }
  }

  template <typename Visitor>
  bool VisitStack(Offset addressInStackRegion, Visitor visitor) const {
    Offset regionBase;
    Offset regionSize;
    size_t stackInfoIndex;
    if (_stacks.FindRange(addressInStackRegion, regionBase, regionSize,
                          stackInfoIndex)) {
      const StackInfo &stackInfo = _stackInfo[stackInfoIndex];
      return visitor(regionBase, (regionBase + regionSize),
                     stackInfo._stackType, stackInfo._stackTop,
                     stackInfo._stackBase, stackInfo._threadNumber);
    }
    return false;
  }

 private:
  struct StackInfo {
    StackInfo(const char *stackType)
        : _stackType(stackType),
          _stackTop(STACK_TOP_UNKNOWN),
          _stackBase(STACK_BASE_UNKNOWN),
          _threadNumber(THREAD_NUMBER_UNKNOWN) {}
    const char *_stackType;
    Offset _stackTop;
    Offset _stackBase;
    size_t _threadNumber;
  };
  RangeMapper<Offset, size_t> _stacks;
  std::vector<StackInfo> _stackInfo;
};
}  // namespace chap
