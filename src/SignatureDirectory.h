// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <map>
#include <set>

/*
 * This keeps mappings from signature to name and name to set of signatures.
 * Note that there are potentially multiple signatures (numbers) for a given
 * name because a signature may be defined in multiple load modules.
 */

namespace chap {
template <class Offset>
class SignatureDirectory {
 public:
  typedef std::map<Offset, std::string> SignatureToNameMap;
  typedef typename SignatureToNameMap::iterator SignatureToNameIterator;
  typedef
      typename SignatureToNameMap::const_iterator SignatureToNameConstIterator;
  typedef std::map<std::string, std::set<Offset> > NameToSignaturesMap;
  typedef typename NameToSignaturesMap::const_iterator
      NameToSignaturesConstIterator;

  SignatureDirectory() : _multipleSignaturesPerName(false) {}

  void MapSignatureToName(Offset signature, std::string name) {
    SignatureToNameIterator it = _signatureToName.find(signature);
    if (it != _signatureToName.end()) {
      /*
       * This signature is already known to be a signature.
       */
      if (it->second == name || name.empty()) {
        /*
         * There is no new information about the name.
         */
        return;
      }
      if (!(it->second).empty()) {
        /*
         * There was a previously known name, which is now no longer
         * associated with the signature.
         */
        _nameToSignatures[it->second].erase(signature);
      }
      it->second = name;
    } else {
      _signatureToName[signature] = name;
    }
    if (!name.empty()) {
      std::set<Offset>& signatures = _nameToSignatures[name];
      signatures.insert(signature);
      if (signatures.size() > 1) {
        _multipleSignaturesPerName = true;
      }
    }
  }

  bool HasMultipleSignaturesPerName() const {
    return _multipleSignaturesPerName;
  }

  bool IsMapped(Offset signature) const {
    return _signatureToName.find(signature) != _signatureToName.end();
  }

  const std::string& Name(Offset signature) const {
    SignatureToNameConstIterator it = _signatureToName.find(signature);
    if (it != _signatureToName.end()) {
      return it->second;
    } else {
      return NO_NAME;
    }
  }

  const std::set<Offset>& Signatures(const std::string& name) const {
    typename NameToSignaturesMap::const_iterator it =
        _nameToSignatures.find(name);
    if (it != _nameToSignatures.end()) {
      return it->second;
    } else {
      return NO_SIGNATURES;
    }
  }

 private:
  bool _multipleSignaturesPerName;
  SignatureToNameMap _signatureToName;
  NameToSignaturesMap _nameToSignatures;
  std::string NO_NAME;
  std::set<Offset> NO_SIGNATURES;
};
}  // namespace chap
