// Copyright (c) 2017,2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <iostream>
#include <set>
#include <sstream>
#include "../CPlusPlus/TypeInfoDirectory.h"
#include "../VirtualAddressMap.h"
#include "Directory.h"
#include "PatternDescriberRegistry.h"
#include "SignatureDirectory.h"

namespace chap {
namespace Allocations {
template <class Offset>
class SignatureChecker {
 public:
  typedef typename Directory<Offset>::Allocation Allocation;
  enum CheckType {
    NO_CHECK_NEEDED,         // This signature checker does nothing
    UNRECOGNIZED_SIGNATURE,  // Error code - indicates unknown signature
    UNRECOGNIZED_PATTERN,    // Error code - indicates unknown pattern
    UNSIGNED_ONLY,           // Must be unsigned
    SIGNATURE_CHECK,         // Must match the specified signature
    PATTERN_CHECK,           // Must match the specified pattern
    TYPE_NAME_NO_INSTANCES,  // A type name has been specified but it has no
                             // instances
    UNRECOGNIZED_ONLY        // Must be unsigned and not patch a pattern
  };
  SignatureChecker(
      const SignatureDirectory<Offset>& directory,
      const CPlusPlus::TypeInfoDirectory<Offset>& typeInfoDirectory,
      const PatternDescriberRegistry<Offset>& patternDescriberRegistry,
      const VirtualAddressMap<Offset>& addressMap, const std::string& signature)
      : _checkType(NO_CHECK_NEEDED),
        _directory(directory),
        _typeInfoDirectory(typeInfoDirectory),
        _patternDescriberRegistry(patternDescriberRegistry),
        _addressMap(addressMap),
        _signature((signature[0] != '%') ? signature : ""),
        _patternName((signature[0] == '%') ? signature.substr(1) : ""),
        _tagIndices(nullptr) {
    if (signature.empty()) {
      return;
    }
    if (signature == "-") {
      _checkType = UNSIGNED_ONLY;
      return;
    }

    if (signature == "?") {
      _checkType = UNRECOGNIZED_ONLY;
      return;
    }

    if (signature[0] == '%') {
      _tagIndices = _patternDescriberRegistry.GetTagIndices(signature);
      if (_tagIndices == nullptr) {
        _checkType = UNRECOGNIZED_PATTERN;
      } else {
        _checkType = PATTERN_CHECK;
      }
      return;
    }

    _signatures = _directory.Signatures(signature);
    Offset numericSignature;
    if (_signatures.empty()) {
      // The directory doesn't have the signature by name.  Check if the
      // signature is numeric.
      // Note that if a class has some name that happens to look OK
      // as hexadecimal, such as BEEF, for example, a requested
      // signature BEEF will be treated as referring to the class
      // name.  For purposes of pseudo signatures, the number can
      // be selected as a sudo-signature by prepending 0, or 0x
      // or anything that chap will parse as hexadecimal but that
      // will make it not match the symbol.
      std::istringstream is(signature);
      is >> std::hex >> numericSignature;
      if (!is.fail() && is.eof()) {
        _signatures.insert(numericSignature);
        _checkType = SIGNATURE_CHECK;
        return;
      }
      if (!_typeInfoDirectory.ContainsName(signature)) {
        _checkType = UNRECOGNIZED_SIGNATURE;
        // TODO: Allow derived types here
        return;
      }
      _checkType = TYPE_NAME_NO_INSTANCES;
      // TODO: Allow derived types here.
      return;
    }
    // TODO: Possibly extend to all derived types.
    _checkType = SIGNATURE_CHECK;
  }
  bool UnrecognizedSignature() const {
    return _checkType == UNRECOGNIZED_SIGNATURE;
  }
  bool UnrecognizedPattern() const {
    return _checkType == UNRECOGNIZED_PATTERN;
  }
  const std::string& GetSignature() { return _signature; }
  const std::string& GetPatternName() { return _patternName; }
  bool Check(typename Directory<Offset>::AllocationIndex index,
             const Allocation& allocation) const {
    switch (_checkType) {
      case NO_CHECK_NEEDED:
        return true;
      case UNRECOGNIZED_SIGNATURE:
        return false;
      case TYPE_NAME_NO_INSTANCES:
        return false;
      case UNRECOGNIZED_PATTERN:
        return false;
      case PATTERN_CHECK:
      case UNSIGNED_ONLY:
      case UNRECOGNIZED_ONLY:
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
          return _tagIndices->find(_patternDescriberRegistry.GetTagIndex(
                     index)) != _tagIndices->end();
        } else if (_checkType == UNSIGNED_ONLY) {
          return ((size < sizeof(Offset)) ||
                  !_directory.IsMapped(*((Offset*)image)));
        } else if (_checkType == UNRECOGNIZED_ONLY) {
          return (((size < sizeof(Offset)) ||
                   !_directory.IsMapped(*((Offset*)image)))) &&
                 (_patternDescriberRegistry.GetTagIndex(index) == 0);
        } else {
          return ((size >= sizeof(Offset)) &&
                  !(_signatures.find(*((Offset*)image)) == _signatures.end()));
        }
    }
    return false;
  }

 private:
  enum CheckType _checkType;
  const SignatureDirectory<Offset>& _directory;
  const CPlusPlus::TypeInfoDirectory<Offset>& _typeInfoDirectory;
  const PatternDescriberRegistry<Offset>& _patternDescriberRegistry;
  const VirtualAddressMap<Offset>& _addressMap;
  const std::string _signature;
  const std::string _patternName;
  std::set<Offset> _signatures;
  const typename PatternDescriberRegistry<Offset>::TagIndices* _tagIndices;
};
}  // namespace Allocations
}  // namespace chap
