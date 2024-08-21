// Copyright (c) 2022-2023 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <unordered_map>
#include <unordered_set>
#include "../Allocations/Directory.h"
#include "../ModuleDirectory.h"
#include "../ModuleImageReader.h"
#include "../VirtualAddressMap.h"
#include "Unmangler.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class TypeInfoDirectory {
 public:
  TypeInfoDirectory(const ModuleDirectory<Offset>& moduleDirectory,
                    const VirtualAddressMap<Offset>& virtualAddressMap,
                    const Allocations::Directory<Offset>& allocationDirectory)
      : _moduleDirectory(moduleDirectory),
        _virtualAddressMap(virtualAddressMap),
        _allocationDirectory(allocationDirectory),
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
      for (const auto& nameAndModuleInfo : _moduleDirectory) {
        FindRemainingTypeInfoInstances(nameAndModuleInfo.second);
      }
      FindTypeNames();
      FillInDerivedTypeInfos();
      ResolveUsedDirectSignatures();
      ResolveUsedSignatures();
    }
    // TODO: Possibly complain if a C++ library is present but the
    // typeinfo objects are not found.
    _isResolved = true;
  }

  bool IsResolved() const { return _isResolved; }

  bool ContainsName(const std::string& name) const {
    return _typeNameToTypeInfos.find(name) != _typeNameToTypeInfos.end();
  }

  void AddSignatures(const std::string& name,
                     std::set<Offset>& signatures) const {
    const auto it = _typeNameToTypeInfos.find(name);
    if (it == _typeNameToTypeInfos.end()) {
      return;
    }
    const std::vector<Offset>& typeInfos = it->second;
    for(Offset typeInfo: typeInfos) {
      const auto itDetails = _detailsMap.find(typeInfo);
      const Details& details = itDetails->second;
      if (details._usedSignatures == nullptr) {
        return;
      }
      for (Offset signature: *(details._usedSignatures)) {
        signatures.insert(signature);
      }
    }
  }

 private:
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  struct Details {
    Details(Offset address)
      : _address(address), _mangledNameAddress(0), _nameReadFromCore(false), _usedSignatures(nullptr) {}
    Details(Offset address, Offset mangledNameAddress)
        : _address(address),
          _mangledNameAddress(mangledNameAddress),
          _nameReadFromCore(false), _usedSignatures(nullptr) {}
    Offset _address;
    Offset _mangledNameAddress;
    std::string _mangledName;
    std::string _unmangledName;
    bool _nameReadFromCore;
    std::unordered_set<Offset> *_usedDirectSignatures;
    std::unordered_set<Offset> *_usedSignatures;
  };
  const ModuleDirectory<Offset>& _moduleDirectory;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  const Allocations::Directory<Offset>& _allocationDirectory;
  bool _isResolved;
  Offset _classTypeTypeInfo;
  Offset _singleInheritanceTypeInfo;
  Offset _multipleInheritanceTypeInfo;
  std::unordered_map<Offset, Details> _detailsMap;
  std::unordered_map<std::string, std::vector<Offset> > _typeNameToTypeInfos;
  /*
   * Map from a given typeinfo to the set of typeinfo entries for any
   * directly derived types.  To save space, in the common case that
   * there are no direct descendants, there should be no corresponding
   * entry in the unordered_map.
   */
  std::unordered_map<Offset, std::unordered_set<Offset> > _directlyDerived;
  /*
   * Map from a given typeinfo to the set of typeinfo entries for any
   * direct base types.  To save space, in the common case that
   * there are no direct bases, there should be no corresponding
   * entry in the unordered_map.
   */
  std::unordered_map<Offset, std::unordered_set<Offset> > _directBases;
  /*
   * Map from a given typeinfo to the set of typeinfo entries for any
   * derived types.  To save space, in the common case that
   * there are no descendants, there should be no corresponding
   * entry in the unordered_map.
   */
  std::unordered_map<Offset, std::unordered_set<Offset> > _derivedTypeInfos;

  bool FindBaseTypeInfoInstances(
      const typename ModuleDirectory<Offset>::ModuleInfo& moduleInfo) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (const auto& range : moduleInfo._ranges) {
      int flags = range._value._flags;
      if ((flags & RangeAttributes::IS_EXECUTABLE) != 0) {
        continue;
      }
      Offset base = range._base;
      Offset limit = range._limit;

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
        Offset singleInheritanceTypeInfoTypeName =
            reader.ReadOffset(singleInheritanceTypeInfo + sizeof(Offset), 0);
        if (singleInheritanceTypeInfoTypeName == 0) {
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
        Offset classTypeTypeInfoTypeName =
            reader.ReadOffset(classTypeTypeInfo + sizeof(Offset), 0);
        if (classTypeTypeInfoTypeName == 0) {
          continue;
        }
        Offset typeInfoTypeInfo =
            reader.ReadOffset(classTypeTypeInfo + 2 * sizeof(Offset), 0);
        if (typeInfoTypeInfo == 0) {
          continue;
        }
        Offset typeInfoTypeName =
            reader.ReadOffset(typeInfoTypeInfo + sizeof(Offset), 0);
        if (typeInfoTypeName == 0) {
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
          Offset multipleInheritanceTypeInfoTypeName = reader.ReadOffset(
              multipleInheritanceTypeInfo + sizeof(Offset), 0);
          if (multipleInheritanceTypeInfoTypeName == 0) {
            continue;
          }
          if (reader.ReadOffset(
                  multipleInheritanceTypeInfo + 2 * sizeof(Offset), 0xbad) !=
              classTypeTypeInfo) {
            continue;
          }
          _detailsMap.emplace(typeInfoTypeInfo, typeInfoTypeInfo)
              .first->second._mangledNameAddress = typeInfoTypeName;
          _classTypeTypeInfo = classTypeTypeInfo;
          _detailsMap.emplace(_classTypeTypeInfo, _classTypeTypeInfo)
              .first->second._mangledNameAddress = classTypeTypeInfoTypeName;
          _singleInheritanceTypeInfo = singleInheritanceTypeInfo;
          _detailsMap
              .emplace(_singleInheritanceTypeInfo, _singleInheritanceTypeInfo)
              .first->second._mangledNameAddress =
              singleInheritanceTypeInfoTypeName;
          _multipleInheritanceTypeInfo = multipleInheritanceTypeInfo;
          _detailsMap
              .emplace(_multipleInheritanceTypeInfo,
                       _multipleInheritanceTypeInfo)
              .first->second._mangledNameAddress =
              multipleInheritanceTypeInfoTypeName;
          return true;
        }
#if 0
        // TODO:  Possibly provide this message if C++ code is known present
        // but type_info information cannot be found.
        std::cerr
            << "Warning: failed to find type_info for multiple inheritance.\n"
               "Analysis of inheritance will not be allowed.\n";
#endif
        return false;
      }
    }
    return false;
  }
  bool FindBaseTypeInfoInstances() {
    bool foundBaseTypes = false;
    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      if (FindBaseTypeInfoInstances(nameAndModuleInfo.second)) {
        foundBaseTypes = true;
      }
    }
    return foundBaseTypes;
  }

  bool CheckOrRegister(Offset typeInfo, Offset typeInfoSignature,
                       Reader& reader) {
    if (_detailsMap.find(typeInfo) != _detailsMap.end()) {
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
      /*
       * TODO: This check doesn't have enough redundancy to actually be
       * confident that a typeinfo has been found.  We could possibly add
       * another check at the point where we try to unmangle the name.
       * We don't want to reject based on lack of signatures that use that
       * typeinfo but we might reject based on lack of vtables that do so.
       */
      _detailsMap.emplace(typeInfo, typeInfo)
          .first->second._mangledNameAddress = typeName;
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
      _directBases[typeInfo].insert(baseTypeInfo);
      _directlyDerived[baseTypeInfo].insert(typeInfo);
      _detailsMap.emplace(typeInfo, typeInfo)
          .first->second._mangledNameAddress = typeName;
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
      for (; listEntry < listLimit; listEntry += 2 * sizeof(Offset)) {
        Offset baseTypeInfo = reader.ReadOffset(listEntry, 0);
        _directBases[typeInfo].insert(baseTypeInfo);
        _directlyDerived[baseTypeInfo].insert(typeInfo);
      }
    }
    _detailsMap.emplace(typeInfo, typeInfo).first->second._mangledNameAddress =
        typeName;
    return true;
  }

  void FindRemainingTypeInfoInstances(
      const typename ModuleDirectory<Offset>::ModuleInfo& moduleInfo) {
    Reader moduleReader(_virtualAddressMap);
    Reader reader(_virtualAddressMap);

    for (const auto& range : moduleInfo._ranges) {
      int flags = range._value._flags;
      if ((flags & RangeAttributes::IS_EXECUTABLE) != 0) {
        continue;
      }
      Offset base = range._base;
      Offset limit = range._limit;
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

  void FindTypeNames() {
    std::map<Offset, std::vector<Offset> > mangledNameAddressToTypeInfos;
    for (const auto& typeInfoAndDetails : _detailsMap) {
      mangledNameAddressToTypeInfos[typeInfoAndDetails.second
                                        ._mangledNameAddress]
          .push_back(typeInfoAndDetails.first);
    }

    std::unordered_map<std::string, std::vector<Offset> >
        mangledNameToTypeInfos;
    Reader reader(_virtualAddressMap);
    ModuleImageReader<Offset> moduleReader(_moduleDirectory);
    char mangledNameBuffer[1000];
    for (auto mangledNameAddressAndTypeInfos : mangledNameAddressToTypeInfos) {
      Offset address = mangledNameAddressAndTypeInfos.first;
      std::vector<Offset>& typeInfosForNameAddress =
          mangledNameAddressAndTypeInfos.second;
      size_t stringLength = reader.ReadCString(address, mangledNameBuffer,
                                               sizeof(mangledNameBuffer));
      if (stringLength == sizeof(mangledNameBuffer)) {
        continue;
      }
      if (stringLength != 0) {
        /*
         * TODO: We can't really trust this name enough to print it, because
         * in the case of a class type that doesn't inherit from other class
         * types the pattern is too weak.
         */
        for (Offset typeInfo : typeInfosForNameAddress) {
          _detailsMap.find(typeInfo)->second._nameReadFromCore = true;
        }
      } else {
        stringLength = moduleReader.ReadCString(address, mangledNameBuffer,
                                                sizeof(mangledNameBuffer));
        if (stringLength == sizeof(mangledNameBuffer)) {
          continue;
        }
        if (stringLength == 0) {
          continue;
        }
      }
      std::string mangledName(mangledNameBuffer, stringLength);
      std::vector<Offset>& typeInfos = mangledNameToTypeInfos[mangledName];
      typeInfos.insert(typeInfos.end(), typeInfosForNameAddress.begin(),
                       typeInfosForNameAddress.end());
    }

    for (const auto& mangledNameAndTypeInfos : mangledNameToTypeInfos) {
      const std::string& mangledName = mangledNameAndTypeInfos.first;
      const std::vector<Offset>& typeInfos = mangledNameAndTypeInfos.second;
      Unmangler<Offset> unmangler(mangledName.c_str(), false);
      const std::string& typeName = unmangler.Unmangled();
      for (Offset typeInfo : typeInfos) {
        auto it = _detailsMap.find(typeInfo);
        it->second._mangledName = mangledName;
        if (!typeName.empty()) {
          it->second._unmangledName = typeName;
        }
      }
      if (!typeName.empty()) {
        std::vector<Offset>& typeInfosForName = _typeNameToTypeInfos[typeName];
        typeInfosForName.insert(typeInfosForName.end(), typeInfos.begin(),
                                typeInfos.end());
      }
    }
  }
  void FillInDerivedTypeInfos() {
    std::deque<Offset> readyToResolveDirectBases;
    std::unordered_map<Offset, size_t> numUnresolvedDirectlyDerived;
    for (auto& kv : _detailsMap) {
      Offset typeInfo = kv.first;
      const auto itDirectlyDerived = _directlyDerived.find(typeInfo);
      if (itDirectlyDerived == _directlyDerived.end()) {
        if (_directBases.find(typeInfo) != _directBases.end()) {
          readyToResolveDirectBases.push_back(typeInfo);
        }
      } else {
        numUnresolvedDirectlyDerived[typeInfo] =
            itDirectlyDerived->second.size();
      }
    }
    while (!readyToResolveDirectBases.empty()) {
      Offset derived = readyToResolveDirectBases.front();
      std::unordered_set<Offset>& bases = _directBases[derived];
      readyToResolveDirectBases.pop_front();
      const auto itDerived = _derivedTypeInfos.find(derived);
      bool derivedHasDerived = (itDerived != _derivedTypeInfos.end());
      for (Offset base : bases) {
        std::unordered_set<Offset>& derivedFromBase = _derivedTypeInfos[base];
        derivedFromBase.insert(derived);
        if (derivedHasDerived) {
          for (Offset indirectlyDerived : itDerived->second) {
            derivedFromBase.insert(indirectlyDerived);
          }
        }
        if (--(numUnresolvedDirectlyDerived[base]) == 0) {
          if (_directBases.find(base) != _directBases.end()) {
            readyToResolveDirectBases.push_back(base);
          }
          numUnresolvedDirectlyDerived.erase(base);
        }
      }
    }
    if (!numUnresolvedDirectlyDerived.empty()) {
      std::cerr << "Warning, some calculated type_info entries appear to be in "
                   "cycles:\n";
      for (const auto typeInfoAndCount : numUnresolvedDirectlyDerived) {
        std::cerr << "0x" << std::hex << typeInfoAndCount.first << "\n";
      }
    }
  }

  void ResolveUsedDirectSignatures() {
    typedef typename Allocations::Directory<Offset>::AllocationIndex
        AllocationIndex;
    typedef typename Allocations::Directory<Offset>::Allocation Allocation;
    AllocationIndex numAllocations = _allocationDirectory.NumAllocations();
    Reader allocationReader(_virtualAddressMap);
    Reader moduleReader(_virtualAddressMap);
    std::unordered_set<Offset> foundSignatures;
    for (AllocationIndex i = 0; i < numAllocations; ++i) {
      const Allocation* allocation = _allocationDirectory.AllocationAt(i);
      if (!allocation->IsUsed()) {
        continue;
      }
      if (allocation->Size() < sizeof(Offset)) {
        continue;
      }
      Offset signature = allocationReader.ReadOffset(allocation->Address(), 0);
      if (signature == 0) {
        continue;
      }
      if (foundSignatures.find(signature) != foundSignatures.end()) {
        continue;
      }
      Offset typeInfoCandidate =
          moduleReader.ReadOffset(signature - sizeof(Offset), 0);
      if (typeInfoCandidate == 0) {
        continue;
      }
      auto it = _detailsMap.find(typeInfoCandidate);
      if (it == _detailsMap.end()) {
        continue;
      }
      Details& details = it->second;
      if (details._usedSignatures == nullptr) {
        details._usedSignatures = new std::unordered_set<Offset>;
      }
      details._usedSignatures->insert(signature);
      
    }
  }
  void ResolveUsedSignatures() {
    std::deque<Offset> readyToResolveDirectBases;
    std::unordered_map<Offset, size_t> numUnresolvedDirectlyDerived;
    for (const auto& kv : _detailsMap) {
      Offset typeInfo = kv.first;
      const auto itDirectlyDerived = _directlyDerived.find(typeInfo);
      if (itDirectlyDerived == _directlyDerived.end()) {
        if (_directBases.find(typeInfo) != _directBases.end()) {
          readyToResolveDirectBases.push_back(typeInfo);
        }
      } else {
        numUnresolvedDirectlyDerived[typeInfo] =
            itDirectlyDerived->second.size();
      }
    }
    while (!readyToResolveDirectBases.empty()) {
      Offset derived = readyToResolveDirectBases.front();
      std::unordered_set<Offset>& bases = _directBases[derived];
      readyToResolveDirectBases.pop_front();

      auto itDerived = _detailsMap.find(derived);
      if (itDerived == _detailsMap.end()) {
        continue;
      }
      Details& details = itDerived->second;



      bool derivedHasUsedSignatures = (details._usedSignatures != nullptr);
      for (Offset base : bases) {
        if (derivedHasUsedSignatures) {
          auto itBase = _detailsMap.find(base);
          if (itBase == _detailsMap.end()) {
            continue;
          }
          Details& detailsForBase = itBase->second;

          if (detailsForBase._usedSignatures == nullptr) {
            detailsForBase._usedSignatures = new std::unordered_set<Offset>;
          }
          std::unordered_set<Offset>& signaturesForBase = *(detailsForBase._usedSignatures);

          for (Offset signature : *(details._usedSignatures)) {
            signaturesForBase.insert(signature);
          }
        }
        if (--(numUnresolvedDirectlyDerived[base]) == 0) {
          if (_directBases.find(base) != _directBases.end()) {
            readyToResolveDirectBases.push_back(base);
          }
          numUnresolvedDirectlyDerived.erase(base);
        }
      }
    }
    if (!numUnresolvedDirectlyDerived.empty()) {
      std::cerr << "Warning, some calculated type_info entries appear to be in "
                   "cycles:\n";
      for (const auto typeInfoAndCount : numUnresolvedDirectlyDerived) {
        std::cerr << "0x" << std::hex << typeInfoAndCount.first << "\n";
      }
    }
  }

};
}  // namespace CPlusPlus
}  // namespace chap
