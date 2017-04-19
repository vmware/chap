// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
namespace chap {
template <class Offset>
class SizedTally {
 public:
  SizedTally(Commands::Context& context, const std::string& itemsLabel)
      : _context(context),
        _itemsLabel(itemsLabel),
        _totalItems(0),
        _totalBytes(0) {}
  ~SizedTally() {
    Commands::Output& output = _context.GetOutput();
    output << std::dec << _totalItems << " " << _itemsLabel << " use 0x"
           << std::hex << _totalBytes << " ("
           << InDecimalWithCommas(_totalBytes) << ")"
           << " bytes.\n";
  }

  bool AdjustTally(Offset size) {
    _totalItems++;
    _totalBytes += size;
    return false;
  }

 private:
  Commands::Context& _context;
  const std::string _itemsLabel;
  Offset _totalItems;
  Offset _totalBytes;

  static std::string InDecimalWithCommas(Offset n) {  // treat as positive
    if (n == 0) {
      return "0";
    } else {
      char chars[22];
      char* p = chars + 22;
      *--p = (char)0;
      int numDigits = 0;
      while (n != 0) {
        if (numDigits > 0 && (numDigits % 3) == 0) {
          *--p = ',';
        }
        numDigits++;
        *--p = (char)(0x30 + n % 10);
        n = n / 10;
      }
      return p;
    }
  }
};
}  // namespace chap
