// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <iostream>
#include <set>
#include <sstream>
#include "../VirtualAddressMap.h"
#include "Finder.h"
#include "PatternRecognizerRegistry.h"
#include "SignatureDirectory.h"

namespace chap {
namespace Allocations {
template <class Offset>
class SignatureChecker {
 public:
  typedef typename Finder<Offset>::Allocation Allocation;
  enum CheckType {
    NO_CHECK_NEEDED,
    UNRECOGNIZED_SIGNATURE,
    UNRECOGNIZED_PATTERN,
    UNSIGNED_ONLY,
    SIGNATURE_CHECK,
    PATTERN_CHECK
  };
  SignatureChecker(
      const SignatureDirectory<Offset>& directory,
      const PatternRecognizerRegistry<Offset>& patternRecognizerRegistry,
      const VirtualAddressMap<Offset>& addressMap, const std::string& signature)
      : _checkType(NO_CHECK_NEEDED),
        _directory(directory),
        _patternRecognizerRegistry(patternRecognizerRegistry),
        _addressMap(addressMap),
        _signature((signature[0] != '%') ? signature : ""),
        _patternName((signature[0] == '%') ? signature.substr(1) : ""),
        _patternRecognizer(0) {
    if (signature.empty()) {
      return;
    }
    if (signature == "-") {
      _checkType = UNSIGNED_ONLY;
      return;
    }

    if (signature[0] == '%') {
      _patternRecognizer = _patternRecognizerRegistry.Find(_patternName);
      if (_patternRecognizer == 0) {
        _checkType = UNRECOGNIZED_PATTERN;
      } else {
        _checkType = PATTERN_CHECK;
      }
      return;
    }

    _signatures = _directory.Signatures(signature);
    Offset numericSignature;
    // Note that if a class has some name that happens to look OK
    // as hexadecimal, such as BEEF, for example, a requested
    // signature BEEF will be treated as referring to the class
    // name.  For purposes of pseudo signatures, the number can
    // be selected as a sudo-signature by prepending 0, or 0x
    // or anything that chap will parse as hexadecimal but that
    // will make it not match the symbol.
    if (_signatures.empty()) {
      std::istringstream is(signature);
      is >> std::hex >> numericSignature;
      if (!is.fail() && is.eof()) {
        _signatures.insert(numericSignature);
      }
    }

    _checkType =
        (_signatures.empty()) ? UNRECOGNIZED_SIGNATURE : SIGNATURE_CHECK;
  }
  bool UnrecognizedSignature() const {
    return _checkType == UNRECOGNIZED_SIGNATURE;
  }
  bool UnrecognizedPattern() const {
    return _checkType == UNRECOGNIZED_PATTERN;
  }
  const std::string& GetSignature() { return _signature; }
  const std::string& GetPatternName() { return _patternName; }
  bool Check(typename Finder<Offset>::AllocationIndex index,
             const Allocation& allocation) const {
    switch (_checkType) {
      case NO_CHECK_NEEDED:
        return true;
      case UNRECOGNIZED_SIGNATURE:
        return false;
      case UNRECOGNIZED_PATTERN:
        return false;
      case PATTERN_CHECK:
      case UNSIGNED_ONLY:
      case SIGNATURE_CHECK:
        const char* image;
        Offset numBytesFound =
            _addressMap.FindMappedMemoryImage(allocation.Address(), &image);
        Offset size = allocation.Size();
        if (numBytesFound < size) {
          /*
           * This is not expected to happen on Linux but could, for
           * example, given null pages in the core.
           */
          size = numBytesFound;
        }

        if (_checkType == PATTERN_CHECK) {
          return _patternRecognizer->Matches(
              index, allocation, ((size < sizeof(Offset)) ||
                                  !_directory.IsMapped(*((Offset*)image))));
        } else if (_checkType == UNSIGNED_ONLY) {
          return ((size < sizeof(Offset)) ||
                  !_directory.IsMapped(*((Offset*)image)));
        } else {
          return ((size >= sizeof(Offset)) &&
                  !(_signatures.find(*((Offset*)image)) == _signatures.end()));
        }
    }
  }

 private:
  enum CheckType _checkType;
  const SignatureDirectory<Offset>& _directory;
  const PatternRecognizerRegistry<Offset>& _patternRecognizerRegistry;
  const VirtualAddressMap<Offset>& _addressMap;
  const std::string _signature;
  const std::string _patternName;
  std::set<Offset> _signatures;
  const PatternRecognizer<Offset>* _patternRecognizer;
};
}  // namespace Allocations
}  // namespace chap
