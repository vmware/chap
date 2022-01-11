// Copyright (c) 2022 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <unordered_map>
#include <unordered_set>
#include "../ModuleDirectory.h"
#include "../VirtualAddressMap.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class TypeInfoFinder {
 public:
  TypeInfoFinder(const ModuleDirectory<Offset>& moduleDirectory,
                 const VirtualAddressMap<Offset>& virtualAddressMap)
      : _moduleDirectory(moduleDirectory),
        _virtualAddressMap(virtualAddressMap),
        _isResolved(false),
        _classTypeTypeInfo(0),
        _singleInheritanceTypeInfo(0),
        _multipleInheritanceTypeInfo(0) {}

  void Resolve() {
    if (_isResolved) {
      abort();
    }
    if (!_moduleDirectory.IsResolved()) {
      abort();
    }

    if (FindBaseTypeInfoInstances()) {
      FindRemainingTypeInfoInstances();
      FillInDerivedTypeInfos();
    }
    // TODO: Possibly complain if a C++ library is present but the
    // typeinfo objects are not found.
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  bool _isResolved;
  Offset _classTypeTypeInfo;
  Offset _singleInheritanceTypeInfo;
  Offset _multipleInheritanceTypeInfo;
  std::unordered_set<Offset> _typeInfos;
  std::unordered_map<Offset, std::unordered_set<Offset> > _derivedTypeInfos;

  bool FindBaseTypeInfoInstances(
      const typename ModuleDirectory<Offset>::RangeToFlags& rangeToFlags) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (typename ModuleDirectory<Offset>::RangeToFlags::const_iterator
             itRange = rangeToFlags.begin();
         itRange != rangeToFlags.end(); ++itRange) {
      int flags = itRange->_value;
      if ((flags & RangeAttributes::IS_EXECUTABLE) != 0) {
        continue;
      }
      Offset base = itRange->_base;
      /*
       * At present the module finding logic can get a lower value for the
       * limit than the true limit.  It is conservative about selecting the
       * limit to avoid tagging too large a range in the partition.  However
       * this conservative estimate is problematic if the array header we
       * are seeking lies between the calculated limit and the real
       * limit.  This code works around this to extend the limit to the
       * last consecutive byte that has the same permission as the last
       * byte in the range.
       */
      Offset limit = _virtualAddressMap.find(itRange->_limit - 1).Limit();

      for (Offset singleInheritanceVTable = base;
           singleInheritanceVTable < limit;
           singleInheritanceVTable += sizeof(Offset)) {
        Offset singleInheritanceTypeInfo =
            moduleReader.ReadOffset(singleInheritanceVTable, 0);
        if (singleInheritanceTypeInfo == 0) {
          continue;
        }
        Offset singleInheritanceSignature =
            singleInheritanceVTable + sizeof(Offset);
        if (reader.ReadOffset(singleInheritanceTypeInfo, 0) !=
            singleInheritanceSignature) {
          continue;
        }
        Offset classTypeTypeInfo = reader.ReadOffset(
            singleInheritanceTypeInfo + 2 * sizeof(Offset), 0);
        if (classTypeTypeInfo == 0) {
          continue;
        }
        if (reader.ReadOffset(classTypeTypeInfo, 0) !=
            singleInheritanceSignature) {
          continue;
        }
        Offset typeInfoTypeInfo =
            reader.ReadOffset(classTypeTypeInfo + 2 * sizeof(Offset), 0);
        if (typeInfoTypeInfo == 0) {
          continue;
        }
        if (reader.ReadOffset(typeInfoTypeInfo + 2 * sizeof(Offset), 0xbad) !=
            0) {
          continue;
        }
        Offset classTypeSignature = reader.ReadOffset(typeInfoTypeInfo, 0);
        if (classTypeSignature == 0) {
          continue;
        }
        if (reader.ReadOffset(classTypeSignature - sizeof(Offset), 0xbad) !=
            classTypeTypeInfo) {
          continue;
        }

        for (Offset multipleInheritanceTypeInfo = base;
             multipleInheritanceTypeInfo < limit;
             multipleInheritanceTypeInfo += sizeof(Offset)) {
          if (multipleInheritanceTypeInfo == singleInheritanceTypeInfo) {
            continue;
          }
          if (reader.ReadOffset(multipleInheritanceTypeInfo, 0xbad) !=
              singleInheritanceSignature) {
            continue;
          }
          if (reader.ReadOffset(
                  multipleInheritanceTypeInfo + 2 * sizeof(Offset), 0xbad) !=
              classTypeTypeInfo) {
            continue;
          }
          _classTypeTypeInfo = classTypeTypeInfo;
          _singleInheritanceTypeInfo = singleInheritanceTypeInfo;
          _multipleInheritanceTypeInfo = multipleInheritanceTypeInfo;
          return true;
        }
        std::cerr
            << "Warning: failed to find type_info for multiple inheritance.\n"
               "Analysis of inheritance will not be allowed.\n";
        return false;
      }
    }
    return false;
  }
  bool FindBaseTypeInfoInstances() {
    typename ModuleDirectory<Offset>::const_iterator itEnd =
        _moduleDirectory.end();
    for (typename ModuleDirectory<Offset>::const_iterator it =
             _moduleDirectory.begin();
         it != itEnd; ++it) {
      if (FindBaseTypeInfoInstances(it->second)) {
        return true;
      }
    }
    return false;
  }

  bool CheckOrRegister(Offset typeInfo, Offset typeInfoSignature,
                       Reader& reader) {
    if (_typeInfos.find(typeInfo) != _typeInfos.end()) {
      return true;
    }
    Offset typeInfoTypeInfo =
        reader.ReadOffset(typeInfoSignature - sizeof(Offset), 0);
    if (typeInfoTypeInfo != _classTypeTypeInfo &&
        typeInfoTypeInfo != _singleInheritanceTypeInfo &&
        typeInfoTypeInfo != _multipleInheritanceTypeInfo) {
      return false;
    }
    Offset typeName = reader.ReadOffset(typeInfo + sizeof(Offset), 0);
    if (typeName == 0) {
      return false;
    }
    std::string moduleName;
    Offset regionBase;
    Offset regionSize;
    Offset relativeVirtualAddress;
    if (!_moduleDirectory.Find(typeName, moduleName, regionBase, regionSize,
                               relativeVirtualAddress)) {
      return false;
    }

    if (typeInfoTypeInfo == _classTypeTypeInfo) {
      _typeInfos.insert(typeInfo);
      return true;
    }
    if (typeInfoTypeInfo == _singleInheritanceTypeInfo) {
      Offset baseTypeInfo = reader.ReadOffset(typeInfo + 2 * sizeof(Offset), 0);
      if (baseTypeInfo == 0) {
        return false;
      }
      Offset baseTypeInfoSignature = reader.ReadOffset(baseTypeInfo, 0);
      if (baseTypeInfoSignature == 0) {
        return false;
      }
      if (!CheckOrRegister(baseTypeInfo, baseTypeInfoSignature, reader)) {
        return false;
      }
      _typeInfos.insert(typeInfo);
      return true;
    }
    uint32_t numBases =
        reader.ReadU32(typeInfo + 2 * sizeof(Offset) + sizeof(uint32_t), 0);
    if (numBases == 0) {
      return false;
    }
    Offset listEntry = typeInfo + 2 * sizeof(Offset) + 2 * sizeof(uint32_t);
    Offset listLimit = listEntry + numBases * sizeof(Offset) * 2;
    if (listLimit <= listEntry) {
      return false;
    }
    for (; listEntry < listLimit; listEntry += 2 * sizeof(Offset)) {
      Offset baseTypeInfo = reader.ReadOffset(listEntry, 0);
      if (baseTypeInfo == 0) {
        return false;
      }
      Offset baseTypeInfoSignature = reader.ReadOffset(baseTypeInfo, 0);
      if (baseTypeInfoSignature == 0) {
        return false;
      }
      if (!CheckOrRegister(baseTypeInfo, baseTypeInfoSignature, reader)) {
        return false;
      }
    }
    _typeInfos.insert(typeInfo);
    return true;
  }

  void FindRemainingTypeInfoInstances(
      const typename ModuleDirectory<Offset>::RangeToFlags& rangeToFlags) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (typename ModuleDirectory<Offset>::RangeToFlags::const_iterator
             itRange = rangeToFlags.begin();
         itRange != rangeToFlags.end(); ++itRange) {
      int flags = itRange->_value;
      if ((flags & RangeAttributes::IS_EXECUTABLE) != 0) {
        continue;
      }
      Offset base = itRange->_base;
      /*
       * At present the module finding logic can get a lower value for the
       * limit than the true limit.  It is conservative about selecting the
       * limit to avoid tagging too large a range in the partition.  However
       * this conservative estimate is problematic if the array header we
       * are seeking lies between the calculated limit and the real
       * limit.  This code works around this to extend the limit to the
       * last consecutive byte that has the same permission as the last
       * byte in the range.
       */
      Offset limit = _virtualAddressMap.find(itRange->_limit - 1).Limit();
      for (Offset typeInfo = base; typeInfo < limit;
           typeInfo += sizeof(Offset)) {
        if (typeInfo == _classTypeTypeInfo ||
            typeInfo == _singleInheritanceTypeInfo ||
            typeInfo == _multipleInheritanceTypeInfo) {
          typeInfo += 2 * sizeof(Offset);
          continue;
        }
        Offset typeInfoSignature = moduleReader.ReadOffset(typeInfo, 0);
        if (typeInfoSignature == 0) {
          continue;
        }
        CheckOrRegister(typeInfo, typeInfoSignature, reader);
      }
    }
  }

  /*
    Ugly: the SignatureDirectory may not know the signature or name of a base
    class because there may be no allocations signed in that way.  This
    means that we must recognize names on the type_info side as well, so that
    we can map from a name to possible signatures for that name.  So maybe
    we should be calculating _nameToSignatures.  First we can fill
    local typeInfoToDirectlyUsedSignatures then member
    _typeInfoToRelevantSignatures
    and _nameToTypeInfos and finally _nameToSignatures.
    How do we want to represent inheritance?
    Do we want to follow inheritance by default?
    How do we enable/disable following inheritance?
    Should we report differences in inheritance for similarly named typeinfo?
    Should we check type_info names where available?
    Should we check differences in identically named type_info?
    We may modify signature checker to not put error if type_info or vtable is
    known but has no instances.
    Use case is signature match, in SignatureChecker.h.
       We set up _signatures differently if inheritance is allowed.
       We have to decide on inheritance for numeric signatures.
       Without inheritance, we are using SignatureDirectory to map from name to
          signature set and having a single entry in the case of numeric
    signatures.
       With inheritance, perhaps we can go from the base set of numeric
    signatures to the
          corresponding type_info structures to any type_info structures that
          inherit from them to the corresponding numeric signatures.
       Can map from vtable -> type_info if present in core (and possibly if in
    binary)
       type_info -> {type_info}
       Can map from type_info to referencing vtables
          SignatureDirectory, in current state, cannot do this, but perhaps the
    SignatureDirectory should
             know about TypeInfo.  At the very least, the SignatureChecker
    should know about TypeInfo, if
             only indirectly via SignatureDirectory, to be able to handle
    inheritance.
          One could do this based on allocations (a to vtp to type_info then
    flip to type_info to vps).
             Do we want TypeInfoFinder to know about allocations?  This doesn't
    seem particularly
             bad
          One could do this based on SignatureDirectory
             This is slightly cheaper due to fact that not all vtables are used
    but it is not
                 clear that we want this sort of dependency.

   */
  void FindRemainingTypeInfoInstances() {
    typename ModuleDirectory<Offset>::const_iterator itEnd =
        _moduleDirectory.end();
    for (typename ModuleDirectory<Offset>::const_iterator it =
             _moduleDirectory.begin();
         it != itEnd; ++it) {
      FindRemainingTypeInfoInstances(it->second);
    }
  }

  void FillInDerivedTypeInfos(Offset derivedTypeInfo, Offset fillInFrom,
                              Reader& reader) {
    if (fillInFrom != derivedTypeInfo) {
      // We don't register a type_info as its own ancestor.
      if (!_derivedTypeInfos[fillInFrom].insert(derivedTypeInfo).second) {
        /*
         * We already propagated the given derived type down to the ancestor
         * via a different path, so we don't have more to do.
         */
        return;
      }
    }
    Offset typeInfoSignature = reader.ReadOffset(fillInFrom);
    Offset typeInfoTypeInfo =
        reader.ReadOffset(typeInfoSignature - sizeof(Offset));
    if (typeInfoTypeInfo == _classTypeTypeInfo) {
      return;
    }
    if (typeInfoTypeInfo == _singleInheritanceTypeInfo) {
      Offset baseTypeInfo = reader.ReadOffset(fillInFrom + 2 * sizeof(Offset));
      FillInDerivedTypeInfos(derivedTypeInfo, baseTypeInfo, reader);
      return;
    }
    // typeInfoTypeInfo must be _multipleInheritanceTypeInfo
    uint32_t numBases =
        reader.ReadU32(fillInFrom + 2 * sizeof(Offset) + sizeof(uint32_t));
    Offset listEntry = fillInFrom + 2 * sizeof(Offset) + 2 * sizeof(uint32_t);
    Offset listLimit = listEntry + numBases * sizeof(Offset) * 2;
    for (; listEntry < listLimit; listEntry += 2 * sizeof(Offset)) {
      Offset baseTypeInfo = reader.ReadOffset(listEntry);
      FillInDerivedTypeInfos(derivedTypeInfo, baseTypeInfo, reader);
    }
  }

  void FillInDerivedTypeInfos() {
    Reader reader(_virtualAddressMap);
    for (Offset typeInfo : _typeInfos) {
      FillInDerivedTypeInfos(typeInfo, typeInfo, reader);
    }
  }
};
}  // namespace CPlusPlus
}  // namespace chap
