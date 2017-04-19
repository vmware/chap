// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <map>
#include "../ProcessImage.h"
#include "LibcMallocAllocationFinder.h"

namespace chap {
namespace Linux {
template <typename OffsetType>
class LinuxProcessImage : public ProcessImage<OffsetType> {
 public:
  typedef OffsetType Offset;
  typedef ProcessImage<Offset> Base;
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef typename AddressMap::Reader Reader;
  typedef typename AddressMap::NotMapped NotMapped;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  LinuxProcessImage(const AddressMap& virtualAddressMap,
                    const ThreadMap<Offset>& threadMap)
      : ProcessImage<OffsetType>(virtualAddressMap, threadMap),
        _symdefsRead(false) {}

  template <typename T>
  struct CompareByAddressField {
    bool operator()(const T& left, const T& right) {
      return left._address < right._address;
    }
  };

  const std::map<Offset, Offset>& GetStaticAnchorLimits() const {
    return _staticAnchorLimits;
  }

 protected:
  void MakeAllocationFinder() const {
    /*
     * This will have to be fixed when we support multiple kinds of
     * allocation finders on Linux.  At present it is assumed that just
     * one memory allocator applies to a given process and that if a
     * different one is used this is done by using LD_PRELOAD to override
     * the libc malloc code.
     */
    Base::_allocationFinder =
        new LibcMallocAllocationFinder<Offset>(Base::_virtualMemoryPartition);

    /*
     * At present this can only be done here because we find the allocations
     * lazily and the current algorithm for static anchor ranges is to
     * assume that all imaged writeable memory that is not otherwise claimed
     * (for example by stack or memory allocators) is OK for anchors.
     */
    FindStaticAnchorRanges();

    /*
     * In Linux processes the current approach is to wait until the
     * allocations have been found, then treat pointers at the start of
     * the allocations to read only memory as signatures.  This means
     * that the signatures can't be identified until the allocations have
     * been found.
     */

    FindSignatures();
  }

  void RefreshSignatureDirectory() const {
    if (Base::_allocationFinder == 0) {
      return;
    }
    if (!_symdefsRead) {
      ReadSymdefsFile();
    }
  }

  void MakeAllocationGraph() const {
    Base::_allocationGraph = new Allocations::Graph<Offset>(
        *Base::_allocationFinder, Base::_threadMap, _staticAnchorLimits,
        (Allocations::ExternalAnchorPointChecker<Offset>*)(0));
  }

 private:
  /*
   * The following is mutable because the symdefs file is read lazily the
   * first tyime it is present and needed.
   */

  mutable bool _symdefsRead;
  /*
   * TODO: This really should not be mutable but it is because static
   * anchor limits are calculated lazily because they are calculated
   * after the allocations are found and the allocations are found
   * lazily.
   */
  mutable std::map<Offset, Offset> _staticAnchorLimits;

  bool ParseOffset(const std::string& s, Offset& value) const {
    if (!s.empty()) {
      std::istringstream is(s);
      uint64_t v;
      is >> std::hex >> v;
      if (!is.fail() && is.eof()) {
        value = v;
        return true;
      }
    }
    return false;
  }

  std::string UnmangledTypeinfoName(char* buffer) const {
    /*
     * Lots of mangled names could start with something else, but typeinfo names
     * are a bit more constrained.
     */
    std::string emptySignatureName;
    char c = buffer[0];
    if (c != 'N' && !((c >= '0') && (c <= '9')) && c != 'S') {
      return emptySignatureName;
    }

    std::stack<char> operationStack;
    std::stack<int> listLengthStack;
    std::stack<std::string> separatorStack;
    std::string unmangledName;
    listLengthStack.push(0);
    separatorStack.push("::");
    std::string lastNamespace;
    for (char* pC = buffer; *pC != '\000'; pC++) {
      char c = *pC;
      if (c == 'I') {  // This starts a list of template arguments;
        unmangledName.append("<");
        operationStack.push(c);
        listLengthStack.push(0);
        separatorStack.push(",");
        continue;
      }

      if (c == 'N') {  // This starts a new namespace-qualified name
        operationStack.push(c);
        listLengthStack.push(0);
        separatorStack.push("::");
        continue;
      }

      if (c == 'E' || c == 'K' || c == 'R' || c == 'P') {
        bool contextFound = true;
        while (!operationStack.empty()) {
          char op = operationStack.top();
          if (op == 'I' || op == 'N') {
            break;
          }
          if (op == 'K') {
            unmangledName.append(" const");
          } else if (op == 'P') {
            unmangledName.append("*");
          } else if (op == 'R') {
            unmangledName.append("&");
          }
          operationStack.pop();
        }

        if (operationStack.empty()) {
          return emptySignatureName;
        }

        if (c == 'E') {
          char op = operationStack.top();
          if (op == 'I') {
            /*
             * Intentionally do not worry about putting ">>" if multiple
             * template
             * argument lists end together because we are not going to compile
             * this stuff and blanks are annoying for parsing the class name
             * as command line input.
             */
            unmangledName.append(">");
          } else {
          }
          operationStack.pop();
          separatorStack.pop();
          listLengthStack.pop();
          if (!listLengthStack.empty()) {
            listLengthStack.top()++;
          }
        } else {
          operationStack.push(c);
          char next = pC[1];
          while (next == 'K' || next == 'R' || next == 'P') {
            operationStack.push(next);
            pC++;
            next = pC[1];
          }
        }
        continue;
      }

      if (listLengthStack.top() > 0) {
        unmangledName.append(separatorStack.top());
      }
      listLengthStack.top()++;

      if ((c >= '1') && (c <= '9')) {
        int length = c - '0';
        for (c = *(++pC); (c >= '0') && (c <= '9'); c = *(++pC)) {
          length = (length * 10) + (c - '0');
        }
        for (int numSeen = 0; numSeen < length; numSeen++) {
          if (pC[numSeen] == 0) {
            return emptySignatureName;
          }
        }
        unmangledName.append(pC, length);
        if (!operationStack.empty() && listLengthStack.top() == 1) {
          lastNamespace.assign(pC, length);
        }
        pC = pC + length - 1;
        continue;
      }

      switch (c) {
        case 'S':
          switch (*(++pC)) {
            case 't':
              unmangledName.append("std");
              break;
            case 's':
              unmangledName.append("std::string");
              break;
            case 'a':
              unmangledName.append("std::allocator");
              break;
            case '_':
              unmangledName.append(lastNamespace);
              break;
            default:
              return emptySignatureName;
          }
          break;
        case 'L':
          // TODO: support constant literals other than just booleans
          if ((pC[1] != 'b') || (pC[2] != '0') && (pC[2] != '1') ||
              pC[3] != 'E') {
            return emptySignatureName;
          }
          pC += 3;
          unmangledName.append((*pC == '1') ? "true" : "false");
          break;
        case 'a':
          unmangledName.append("signed char");
          break;
        case 'b':
          unmangledName.append("bool");
          break;
        case 'c':
          unmangledName.append("char");
          break;
        case 'd':
          unmangledName.append("double");
          break;
        case 'e':
          unmangledName.append("long double");
          break;
        case 'f':
          unmangledName.append("float");
          break;
        case 'g':
          unmangledName.append("__float128");
          break;
        case 'h':
          unmangledName.append("unsigned char");
          break;
        case 'i':
          unmangledName.append("int");
          break;
        case 'j':
          unmangledName.append("unsigned int");
          break;
        case 'l':
          unmangledName.append("long");
          break;
        case 'm':
          unmangledName.append("unsigned long");
          break;
        case 'n':
          unmangledName.append("__int128");
          break;
        case 'o':
          unmangledName.append("unsigned __int128");
          break;
        case 's':
          unmangledName.append("short");
          break;
        case 't':
          unmangledName.append("unsigned short");
          break;
        case 'u':
          unmangledName.append("unsigned long long");
          break;
        case 'v':
          unmangledName.append("void");
          break;
        case 'w':
          unmangledName.append("wchar_t");
          break;
        case 'x':
          unmangledName.append("long long");
          break;
        case 'y':
          unmangledName.append("unsigned long long");
          break;
        case 'z':
          unmangledName.append("...");
          break;
        default:
          return emptySignatureName;
      }
    }
    if (operationStack.empty()) {
      return unmangledName;
    }

    return emptySignatureName;
  }

  std::string GetUnmangledTypeinfoName(Offset signature) const {
    std::string emptySignatureName;
    Offset typeInfoPointerAddress = signature - sizeof(Offset);

    typename VirtualAddressMap<Offset>::Reader reader(Base::_virtualAddressMap);
    try {
      Offset typeInfoAddress = reader.ReadOffset(typeInfoPointerAddress);
      Offset typeInfoNameAddress =
          reader.ReadOffset(typeInfoAddress + sizeof(Offset));
      char buffer[1000];
      buffer[sizeof(buffer) - 1] = '\000';
      int numToCopy = sizeof(buffer) - 1;
      typename VirtualAddressMap<Offset>::const_iterator it =
          Base::_virtualAddressMap.find(typeInfoNameAddress);
      if (it != Base::_virtualAddressMap.end()) {
        const char* image = it.GetImage();
        if (image != (const char*)(0)) {
          Offset maxToCopy = it.Limit() - typeInfoNameAddress - 1;
          if (numToCopy > maxToCopy) {
            numToCopy = maxToCopy;
          }
          memcpy(buffer, image + (typeInfoNameAddress - it.Base()), numToCopy);

          return UnmangledTypeinfoName(buffer);
        }
      }
    } catch (typename VirtualAddressMap<Offset>::NotMapped&) {
    }
    return "";
  }

  bool ReadSymdefsFile() const {
    // TODO: This implies just one image per file.
    std::string symDefsPath(
        Base::_virtualAddressMap.GetFileImage().GetFileName());
    symDefsPath.append(".symdefs");
    std::ifstream symDefs;
    symDefs.open(symDefsPath.c_str());
    if (symDefs.fail()) {
      return false;
    }
    std::string line;
    Offset signature = 0;
    Offset anchor = 0;
    bool expectStackPointer = false;

    while (getline(symDefs, line, '\n')) {
      size_t lastNonBlank = line.find_last_not_of(' ');
      if (lastNonBlank == std::string::npos) {
        continue;
      }
      if (lastNonBlank != line.size() - 1) {
        line.erase(lastNonBlank + 1);
      }

      if (line.find("SIGNATURE ") == 0) {
        std::string signatureString(line, 10);
        if (!ParseOffset(signatureString, signature)) {
          std::cerr << "\"" << signatureString
                    << "\" is not a valid hexadecimal number\"" << std::endl;
          signature = 0;
        }
        continue;
      }
      if (line.find("ANCHOR ") == 0) {
        expectStackPointer = false;
        std::string anchorString(line, 7);
        if (!ParseOffset(anchorString, anchor)) {
          std::cerr << "\"" << anchorString
                    << "\" is not a valid hexadecimal number\"" << std::endl;
          anchor = 0;
        }
        continue;
      }
      if (line.find("No symbol matches") != std::string::npos || line.empty()) {
        signature = 0;
        anchor = 0;
      }

      if (signature != 0) {
        size_t defStart = 0;
        size_t forPos = line.find(" for ");
        if (forPos != std::string::npos) {
          defStart = forPos + 5;
        }
        size_t defEnd = line.find(" in section");
        size_t plusPos = line.find(" + ");
        defEnd = plusPos;
        std::string name(line.substr(defStart, defEnd - defStart));
        Base::_signatureDirectory.MapSignatureToName(signature, name);
        signature = 0;
      } else if (anchor != 0) {
        size_t defEnd = line.find(" in section");
        //??? _anchorToName[anchor] = line.substr(0, defEnd);
        anchor = 0;
      }
    }
    symDefs.close();
    _symdefsRead = true;
    return true;
  }

  /*
   * TODO: This is declared as const only because it is done lazily.
   * It is done lazily because finding allocations is done lazily.  Fix
   * this.
   */

  void FindSignatures() const {
    std::string emptyName;
    bool writeSymreqs = true;
    std::string symReqsPath(
        Base::_virtualAddressMap.GetFileImage().GetFileName());
    symReqsPath.append(".symreqs");
    std::string symDefsPath(
        Base::_virtualAddressMap.GetFileImage().GetFileName());
    symDefsPath.append(".symdefs");
    std::ifstream symReqs;
    symReqs.open(symReqsPath.c_str());
    std::ofstream gdbScriptFile;
    if (!symReqs.fail()) {
      writeSymreqs = false;
    } else {
      gdbScriptFile.open(symReqsPath.c_str());
      if (gdbScriptFile.fail()) {
        writeSymreqs = false;
        std::cerr << "Unable to open " << symReqsPath << " for writing.\n";
      } else {
        gdbScriptFile << "set logging file " << symDefsPath << '\n';
        gdbScriptFile << "set logging overwrite 1\n";
        gdbScriptFile << "set logging redirect 1\n";
        gdbScriptFile << "set logging on\n";
        gdbScriptFile << "set height 0\n";
      }
    }
    const Allocations::Finder<Offset>& finder = *(Base::_allocationFinder);
    typename Allocations::Finder<Offset>::AllocationIndex numAllocations =
        finder.NumAllocations();
    typename VirtualAddressMap<Offset>::Reader reader(Base::_virtualAddressMap);
    typename VirtualAddressMap<Offset>::const_iterator itEnd =
        Base::_virtualAddressMap.end();
    for (typename Allocations::Finder<Offset>::AllocationIndex i = 0;
         i < numAllocations; ++i) {
      const typename Allocations::Finder<Offset>::Allocation* allocation =
          finder.AllocationAt(i);
      if (!allocation->IsUsed() || (allocation->Size() < sizeof(Offset))) {
        continue;
      }
      Offset signature = reader.ReadOffset(allocation->Address());
      if (Base::_signatureDirectory.IsMapped(signature)) {
        continue;
      }
      if (((signature & (sizeof(Offset) - 1)) != 0) || (signature == 0)) {
        continue;
      }

      typename VirtualAddressMap<Offset>::const_iterator it =
          Base::_virtualAddressMap.find(signature);
      if (it == itEnd) {
        continue;
      }

      if ((it.Flags() & RangeAttributes::IS_WRITABLE) != 0) {
        continue;
      }

      std::string typeinfoName = GetUnmangledTypeinfoName(signature);

      Base::_signatureDirectory.MapSignatureToName(signature, typeinfoName);
      if (writeSymreqs && typeinfoName.empty()) {
        gdbScriptFile << "printf \"SIGNATURE " << std::hex << signature
                      << "\\n\"" << '\n'
                      << "info symbol 0x" << signature << '\n';
      }
    }
#if 0
      // ??? In old approach we need anchor requests in .symreqs
      // ??? as well.
      // ??? In the new approach this is awkward because the static
      // ??? anchor points are now calculated by the graph, which is
      // ??? in turn calculated lazily.  We want the anchor points
      // ??? somewhat earlier.
      if (_staticAnchorPoints.size() > 1000000) {
         gdbScriptFile << "# Too many anchors were found ("
                       << dec << _staticAnchorPoints.size()
                       << ") ... omitting anchor points\n";
      }
      for (AnchorPointMapConstIterator it = _staticAnchorPoints.begin();
           it!= _staticAnchorPoints.end(); ++it) {
         for (OffsetVectorConstIterator itVec = it->second.begin();
              itVec != it->second.end(); ++itVec) {
            gdbScriptFile << "printf \"ANCHOR " << hex << *itVec << "\\n\""
                          << '\n'
                          << "info symbol 0x" << hex << *itVec << '\n';
         }
      }
      // TODO - possibly handle failed I/O in some way.
#endif
    if (writeSymreqs) {
      gdbScriptFile << "set logging off\n";
      gdbScriptFile << "set logging overwrite 0\n";
      gdbScriptFile << "set logging redirect 0\n";
      gdbScriptFile << "printf \"output written to " << symDefsPath << "\\n\""
                    << '\n';
      gdbScriptFile.close();
    }
  }
  void FindStaticAnchorRanges() const {
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator
        itEnd = Base::_virtualMemoryPartition.EndUnclaimedImages();
    typename VirtualMemoryPartition<Offset>::UnclaimedImagesConstIterator it =
        Base::_virtualMemoryPartition.BeginUnclaimedImages();
    for (; it != itEnd; ++it) {
      if ((it->_value &
           (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) ==
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_WRITABLE)) {
        _staticAnchorLimits[it->_base] = it->_limit;
      }
    }
  }
};
}  // namespace Linux
}  // namespace chap
