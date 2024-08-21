// Copyright (c) 2019 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#include <map>
#include <set>
#include <string>

using namespace std;

class MapOrSetPatternTest {
 public:
  MapOrSetPatternTest() {
    _stringToIntOneElement["a"] = 1;
    _stringToIntTwoElements["e1"] = 0x51;
    _stringToIntTwoElements["e2"] = 0x52;
    _stringsTwoElements.insert("f2");
    _stringsTwoElements.insert("f1");
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
      _ints16Elements.insert((i << 16) | ((i * 61) & 0xfff));
    }
  }

 private:
  map<string, int> _stringToIntOneElement;
  map<string, int> _stringToIntTwoElements;
  set<string> _stringsTwoElements;
  map<int, int> _intToIntTwoElements;
  set<int> _intsTwoElements;
  map<string, int> _stringToIntThreeElements;
  set<string> _stringsThreeElements;
  map<int, int> _intToIntThreeElements;
  set<int> _intsThreeElements;
  set<int> _ints16Elements;
};

MapOrSetPatternTest anchoredStatically;

int main(int, char **, char **) {
  MapOrSetPatternTest anchoredOnStack;
  MapOrSetPatternTest *anchoredDynamically =
      new MapOrSetPatternTest();
  *((int *)(0)) = 92;
}
