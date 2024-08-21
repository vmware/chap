// Copyright (c) 2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <string>
#include <unordered_map>
#include "../VirtualAddressMap.h"

namespace chap {
namespace Python {
template <class Offset>
class TypeDirectory {
 public:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;

  TypeDirectory(const VirtualAddressMap<Offset>& virtualAddressMap)
      : _virtualAddressMap(virtualAddressMap) {}

  const std::string& RegisterType(Offset pythonType,
                                  const std::string& suggestedName) {
    auto iteratorAndInserted = _typeToName.emplace(pythonType, suggestedName);
    auto& it = iteratorAndInserted.first;
    if (iteratorAndInserted.second) {
      // The type was not previously known.
      const char* typeImage;
      Offset numBytesFound;
      std::string name;
      numBytesFound =
          _virtualAddressMap.FindMappedMemoryImage(pythonType, &typeImage);
      if (numBytesFound < 4 * sizeof(Offset)) {
        std::cerr << "Warning: Python type at 0x" << std::hex << pythonType
                  << " is not fully mapped in memory.\n";
      }
      Offset nameAddressFromType = ((Offset*)typeImage)[3];
      const char* nameImage;
      numBytesFound = _virtualAddressMap.FindMappedMemoryImage(
          nameAddressFromType, &nameImage);
      if (numBytesFound >= 2) {
        Offset nameLength = strnlen(nameImage, numBytesFound);
        if (nameLength < numBytesFound) {
          it->second.assign(nameImage);
        }
      }
    } else {
      // The type was previously known but perhaps the name was not.
      if (!suggestedName.empty()) {
        if (it->second.empty()) {
          it->second = suggestedName;
        }
      }
    }
    return it->second;
  }

  const std::string& GetTypeName(Offset pythonType) const {
    typename std::unordered_map<Offset, std::string>::const_iterator it =
        _typeToName.find(pythonType);
    return (it == _typeToName.end()) ? NO_TYPE_NAME : it->second;
  }

  bool HasType(Offset pythonType) const {
    return (_typeToName.find(pythonType) != _typeToName.end());
  }

 private:
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  std::unordered_map<Offset, std::string> _typeToName;
  std::string NO_TYPE_NAME;
};

}  // namespace Python
}  // namespace chap
