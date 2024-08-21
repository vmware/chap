// Copyright (c) 2019 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#include <deque>
using namespace std;

template <typename T>
class DequePatternTest {
 public:
  DequePatternTest() {
    T base = 0;
    _1m0.push_back(base++);
    base = AddThenRemove(2, 2, base, _2m2);
    base = AddThenRemove(64, 0, base, _64m0);
    base = AddThenRemove(64, 64, base, _64m64);
    base = AddThenRemove(128, 0, base, _128m0);
    base = AddThenRemove(511, 256, base, _511m256);
    base = AddThenRemove(384, 384, base, _384m384);
    base = AddThenRemove(255, 255, base, _255m255p255m255);
    base = AddThenRemove(255, 255, base, _255m255p255m255);
  }

 private:
  T AddThenRemove(size_t numToAdd, size_t numToRemove, T base, deque<T> &d) {
    T next = base;
    for (size_t i = 0; i < numToAdd; i++) {
      d.push_back(next++);
    }
    for (size_t i = 0; i < numToRemove; i++) {
      d.pop_front();
    }
    return next;
  }

  deque<T> _alwaysEmpty;
  deque<T> _1m0;
  deque<T> _2m2;
  deque<T> _64m0;
  deque<T> _64m64;
  deque<T> _128m0;
  deque<T> _511m256;
  deque<T> _384m384;
  deque<T> _255m255p255m255;
};

struct TestWithMultipleTypes {
  DequePatternTest<char> _t1;
  DequePatternTest<short> _t2;
  DequePatternTest<long> _t3;
  DequePatternTest<char *> _t4;
};
TestWithMultipleTypes anchoredStatically;

int main(int, char **, char **) {
  TestWithMultipleTypes anchoredOnStack;
  TestWithMultipleTypes *anchoredDynamically = new TestWithMultipleTypes();
  *((int *)(0)) = 92;
}
