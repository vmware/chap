// Copyright (c) 2019 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#include <unordered_map>
#include <unordered_set>
using namespace std;

class UnorderedMapOrSetPatternTest {
 public:
  UnorderedMapOrSetPatternTest() {
    _stringToIntOneElement["a"] = 1;
    _stringsOneElement.insert("b");
    _intToIntOneElement[3] = 3;
    _intsOneElement.insert(4);
    _stringToIntTwoElements["e1"] = 0x51;
    _stringToIntTwoElements["e2"] = 0x52;
    _stringsTwoElements.insert("f1");
    _stringsTwoElements.insert("f2");
    _intToIntTwoElements[0x71] = 0x71;
    _intToIntTwoElements[0x72] = 0x72;
    _intsTwoElements.insert(0x81);
    _intsTwoElements.insert(0x82);
    _stringToIntThreeElements["i1"] = 0x91;
    _stringToIntThreeElements["i2"] = 0x92;
    _stringToIntThreeElements["i3"] = 0x93;
    _stringsThreeElements.insert("j1");
    _stringsThreeElements.insert("j2");
    _stringsThreeElements.insert("j3");
    _intToIntThreeElements[0xb1] = 0xb1;
    _intToIntThreeElements[0xb2] = 0xb2;
    _intToIntThreeElements[0xb3] = 0xb3;
    _intsThreeElements.insert(0xc1);
    _intsThreeElements.insert(0xc2);
    _intsThreeElements.insert(0xc3);
    for (int i = 0; i < 16; i++) {
      _ints16Elements.insert(i << 8);
    }
    _had1Now0.insert(0xd);
    _had1Now0.erase(0xd);
    for (int i = 0xe0; i < 0xe5; i++) {
      _had5Now0.insert(i);
    }
    for (int i = 0xe0; i < 0xe5; i++) {
      _had5Now0.erase(i);
    }
    for (int i = 0xe0; i < 0xea; i++) {
      _had10Now0.insert(i);
    }
    for (int i = 0xe0; i < 0xea; i++) {
      _had10Now0.erase(i);
    }
    _quarterDensity1.max_load_factor(0.25);
    _quarterDensity1.insert(0xf0);
    _thirdDensity1.max_load_factor(0.333333333);
    _thirdDensity1.insert(0xf1);
    _tenthDensity1.max_load_factor(0.1);
    _tenthDensity1.insert(0xf2);
    _doubleDensity1.max_load_factor(2.0);
    _doubleDensity1.insert(0xf3);
    _doubleDensity2.max_load_factor(2.0);
    _doubleDensity2.insert(0xf4);
    _doubleDensity2.insert(0xf5);
    _doubleDensity3.max_load_factor(2.0);
    _doubleDensity3.insert(0xf6);
    _doubleDensity3.insert(0xf7);
    _doubleDensity3.insert(0xf8);
    _1Point5Density1.max_load_factor(1.5);
    _1Point5Density1.insert(0xf9);
    _1Point5Density2.max_load_factor(1.5);
    _1Point5Density2.insert(0xfa);
    _1Point5Density2.insert(0xfb);
  }

 private:
  unordered_map<string, int> _stringToIntAlwaysEmpty;
  unordered_set<string> _stringsAlwaysEmpty;
  unordered_map<int, int> _intToIntAlwaysEmpty;
  unordered_set<int> _intsAlwaysEmpty;
  unordered_map<string, int> _stringToIntOneElement;
  unordered_set<string> _stringsOneElement;
  unordered_map<int, int> _intToIntOneElement;
  unordered_set<int> _intsOneElement;
  unordered_map<string, int> _stringToIntTwoElements;
  unordered_set<string> _stringsTwoElements;
  unordered_map<int, int> _intToIntTwoElements;
  unordered_set<int> _intsTwoElements;
  unordered_map<string, int> _stringToIntThreeElements;
  unordered_set<string> _stringsThreeElements;
  unordered_map<int, int> _intToIntThreeElements;
  unordered_set<int> _intsThreeElements;
  unordered_set<int> _ints16Elements;
  unordered_set<int> _had1Now0;
  unordered_set<int> _had5Now0;
  unordered_set<int> _had10Now0;
  unordered_set<int> _quarterDensity1;
  unordered_set<int> _thirdDensity1;
  unordered_set<int> _tenthDensity1;
  unordered_set<int> _doubleDensity1;
  unordered_set<int> _doubleDensity2;
  unordered_set<int> _doubleDensity3;
  unordered_set<int> _1Point5Density1;
  unordered_set<int> _1Point5Density2;
};

UnorderedMapOrSetPatternTest anchoredStatically;

int main(int, char **, char **) {
  UnorderedMapOrSetPatternTest anchoredOnStack;
  UnorderedMapOrSetPatternTest *anchoredDynamically =
      new UnorderedMapOrSetPatternTest();
  *((int *)(0)) = 92;
}
