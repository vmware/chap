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
namespace Allocations {
template <class Offset>
class SignatureDirectory {
 public:
  enum Status {
    UNWRITABLE_PENDING_SYMDEFS,
    UNWRITABLE_MISSING_FROM_SYMDEFS,
    VTABLE_WITH_NAME_FROM_SYMDEFS,
    UNWRITABLE_WITH_NAME_FROM_SYMDEFS,
    VTABLE_WITH_NAME_FROM_PROCESS_IMAGE,
    WRITABLE_VTABLE_WITH_NAME_FROM_PROCESS_IMAGE,
    VTABLE_WITH_NAME_FROM_BINARY,
    VTABLE_WITH_NAME_FROM_BINDEFS
  };

  typedef std::map<Offset, std::pair<std::string, Status> >
      SignatureNameAndStatusMap;
  typedef typename SignatureNameAndStatusMap::iterator
      SignatureNameAndStatusIterator;
  typedef typename SignatureNameAndStatusMap::const_iterator
      SignatureNameAndStatusConstIterator;
  typedef std::map<std::string, std::set<Offset> > NameToSignaturesMap;
  typedef typename NameToSignaturesMap::const_iterator
      NameToSignaturesConstIterator;

  SignatureDirectory() : _multipleSignaturesPerName(false) {}

  void MapSignatureNameAndStatus(Offset signature, std::string name,
                                 Status status) {
    SignatureNameAndStatusIterator it = _signatureToName.find(signature);
    if (it != _signatureToName.end()) {
      std::string& knownName = it->second.first;
      Status& knownStatus = it->second.second;

      /*
       * This signature is already known to be a signature.
       */
      if (name.empty() ? (knownStatus == status) : (name == knownName)) {
        /*
         * There is no new information about the name.
         */
        return;
      }
      if (!knownName.empty()) {
        /*
         * There was a previously known name, which is now no longer
         * associated with the signature.
         */
        _nameToSignatures[knownName].erase(signature);
      }
      knownName = name;
      knownStatus = status;
    } else {
      _signatureToName[signature] = std::make_pair(name, status);
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
    SignatureNameAndStatusConstIterator it = _signatureToName.find(signature);
    if (it != _signatureToName.end()) {
      return it->second.first;
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

  SignatureNameAndStatusConstIterator BeginSignatures() const {
    return _signatureToName.begin();
  }

  SignatureNameAndStatusConstIterator EndSignatures() const {
    return _signatureToName.end();
  }

 private:
  bool _multipleSignaturesPerName;
  SignatureNameAndStatusMap _signatureToName;
  NameToSignaturesMap _nameToSignatures;
  std::string NO_NAME;
  std::set<Offset> NO_SIGNATURES;
};
}  // namespace Allocations
}  // namespace chap
