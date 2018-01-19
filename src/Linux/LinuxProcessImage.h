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
                    const ThreadMap<Offset>& threadMap,
                    bool truncationCheckOnly)
      : ProcessImage<OffsetType>(virtualAddressMap, threadMap),
        _symdefsRead(false) {
    if (!truncationCheckOnly) {
      FindModules();

      /*
       * Make the allocation finder eagerly unless chap is being used only
       * to determine whether the core is truncated, in which case there
       * won't be any need to find the allocations.
       */

      Base::_allocationFinder =
          new LibcMallocAllocationFinder<Offset>(Base::_virtualMemoryPartition);

      /*
       * At present this can only be done here because we may find the
       * allocations
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

      Base::_allocationGraph = new Allocations::Graph<Offset>(
        *Base::_allocationFinder, Base::_threadMap, _staticAnchorLimits,
        (Allocations::ExternalAnchorPointChecker<Offset>*)(0));
    }
  }

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

  void FindModules() {
    Offset executableAddress = 0;
    typename AddressMap::const_iterator itEnd = Base::_virtualAddressMap.end();

    for (typename AddressMap::const_iterator it =
             Base::_virtualAddressMap.begin();
         it != itEnd; ++it) {
      int flags = it.Flags();
      const char* image = it.GetImage();
      Offset base = it.Base();
      Offset limit = it.Limit();
      if ((flags & RangeAttributes::PERMISSIONS_MASK) !=
          (RangeAttributes::IS_READABLE | RangeAttributes::IS_EXECUTABLE |
           RangeAttributes::HAS_KNOWN_PERMISSIONS)) {
        continue;
      }
      if (image == 0) {
        continue;
      }
      if (strncmp((const char*)image, ELFMAG, SELFMAG)) {
        continue;
      }

      const unsigned char elfClass = ((const unsigned char*)image)[EI_CLASS];
      bool acceptElfClass = true;
      if (elfClass == ELFCLASS64) {
        const Elf64_Ehdr* elfHeader = (const Elf64_Ehdr*)(image);
        if (elfHeader->e_type == ET_EXEC) {
          if (executableAddress == 0) {
            executableAddress = base;
          } else {
            std::cerr << "An image of an ELF executable was found at both 0x"
                      << std::hex << executableAddress << " and 0x" << base
                      << ".\n";
            std::cerr
                << "This is unexpected but probably won't break anything.\n";
          }
        } else if (elfHeader->e_type != ET_DYN) {
          continue;
        }
        if (sizeof(Offset) != 8) {
          std::cerr << "Image of 64 bit library or executable currently not "
                       "supported with 32 bit core.\n";
          acceptElfClass = false;
        }
      } else if (elfClass == ELFCLASS32) {
        const Elf32_Ehdr* elfHeader = (const Elf32_Ehdr*)(image);
        if (elfHeader->e_type == ET_EXEC) {
          if (executableAddress == 0) {
            executableAddress = base;
          } else {
            std::cerr << "An image of an ELF executable was found at both 0x"
                      << std::hex << executableAddress << " and 0x" << base
                      << ".\n";
            std::cerr
                << "This is unexpected but probably won't break anything.\n";
          }
        } else if (elfHeader->e_type != ET_DYN) {
          continue;
        }
        if (sizeof(Offset) != 4) {
          std::cerr << "Image of 32 bit library or executable currently not "
                       "supported with 64 bit core.\n";
          acceptElfClass = false;
        }
      } else {
        std::cerr << "Elf class " << std::dec << elfClass
                  << " will not be included in module directory.\n";
        acceptElfClass = false;
      }
      if (!acceptElfClass) {
        std::cerr << "Image at 0x" << std::hex << base
                  << " will be excluded from module directory.\n";
        continue;
      }

      const char* name = (char*)(0);
      if (executableAddress == base) {
        name = "main executable";
      }
      Offset dynStrAddr = 0;
      Offset nameInDynStr = 0;
      if (elfClass == ELFCLASS64) {
        const Elf64_Ehdr* elfHeader = (const Elf64_Ehdr*)(image);
        int entrySize = elfHeader->e_phentsize;
        Offset minimumExpectedRegionSize =
            elfHeader->e_phoff + (elfHeader->e_phnum * entrySize);
        const char* headerImage = image + elfHeader->e_phoff;
        const char* headerLimit = image + minimumExpectedRegionSize;
        if (it.Size() < minimumExpectedRegionSize) {
          std::cerr << "Contiguous image of module at 0x" << std::hex << base
                    << " is only " << it.Size() << " bytes.\n";
          continue;
        }

        bool firstPTLoadFound = false;
        bool adjustByBase = false;
        Offset dynBase = 0;
        Offset dynSize = 0;
        for (; headerImage < headerLimit; headerImage += entrySize) {
          Elf64_Phdr* programHeader = (Elf64_Phdr*)(headerImage);
          Offset vAddr = programHeader->p_vaddr;
          if (programHeader->p_type == PT_LOAD) {
            if (!firstPTLoadFound) {
              firstPTLoadFound = true;
              adjustByBase = (vAddr == 0);
            }
            if (adjustByBase) {
              vAddr += base;
            }
            Offset loadLimit =
                ((vAddr + programHeader->p_memsz) + 0xfff) & ~0xfff;
            if (limit < loadLimit) {
              limit = loadLimit;
            }
          } else if (programHeader->p_type == PT_DYNAMIC &&
                     base != executableAddress) {
            /*
             * Defer the rest of the processing, based on the notion that
             * we may not have seen the first PT_LOAD yet and don't know
             * whether to adjust the base.
             */
            dynBase = vAddr;
            dynSize = programHeader->p_memsz;
          }
        }
        if (dynBase != 0) {
          if (adjustByBase) {
            dynBase += base;
          }
          const char* dynImage = 0;
          Offset numBytesFound = Base::_virtualAddressMap.FindMappedMemoryImage(
              dynBase, &dynImage);
          if (numBytesFound < dynSize) {
#if 0
            // It is a regrettably common case that the last thing that
            // looks like the image of a shared library refers to a
            // PT_DYNAMIC section that is not actually in the core.
            // For now, don't complain because it happens pretty much
            // for every core.
            std::cerr << "Only 0x" << std::hex << numBytesFound
                      << " bytes found for PT_DYNAMIC section at 0x"
                      << dynBase << "\n... for image at 0x" << base << "\n";
#endif
            continue;
          }
          int numDyn = dynSize / sizeof(Elf64_Dyn);
          const Elf64_Dyn* dyn = (Elf64_Dyn*)(dynImage);
          const Elf64_Dyn* dynLimit = dyn + numDyn;
          for (; dyn < dynLimit; dyn++) {
            if (dyn->d_tag == DT_STRTAB) {
              dynStrAddr = (Offset)dyn->d_un.d_ptr;
            } else if (dyn->d_tag == DT_SONAME) {
              nameInDynStr = (Offset)dyn->d_un.d_ptr;
            }
          }
        } else {
          if (base != executableAddress) {
            std::cerr << "Library image at 0x" << std::hex << base
                      << " has no PT_DYNAMIC section.\n";
          }
        }
      } else {
        const Elf32_Ehdr* elfHeader = (const Elf32_Ehdr*)(image);
        int entrySize = elfHeader->e_phentsize;
        Offset minimumExpectedRegionSize =
            elfHeader->e_phoff + (elfHeader->e_phnum * entrySize);
        const char* headerImage = image + elfHeader->e_phoff;
        const char* headerLimit = image + minimumExpectedRegionSize;
        if (it.Size() < minimumExpectedRegionSize) {
          std::cerr << "Contiguous image of module at 0x" << std::hex << base
                    << " is only " << it.Size() << " bytes.\n";
          continue;
        }

        bool firstPTLoadFound = false;
        bool adjustByBase = false;
        Offset dynBase = 0;
        Offset dynSize = 0;
        for (; headerImage < headerLimit; headerImage += entrySize) {
          Elf32_Phdr* programHeader = (Elf32_Phdr*)(headerImage);
          Offset vAddr = programHeader->p_vaddr;
          if (programHeader->p_type == PT_LOAD) {
            if (!firstPTLoadFound) {
              firstPTLoadFound = true;
              adjustByBase = (vAddr == 0);
            }
            if (adjustByBase) {
              vAddr += base;
            }
            Offset loadLimit =
                ((vAddr + programHeader->p_memsz) + 0xfff) & ~0xfff;
            if (limit < loadLimit) {
              limit = loadLimit;
            }
          } else if (programHeader->p_type == PT_DYNAMIC &&
                     base != executableAddress) {
            /*
             * Defer the rest of the processing, based on the notion that
             * we may not have seen the first PT_LOAD yet and don't know
             * whether to adjust the base.
             */
            dynBase = vAddr;
            dynSize = programHeader->p_memsz;
          }
        }
        if (dynBase != 0) {
          if (adjustByBase) {
            dynBase += base;
          }
          const char* dynImage = 0;
          Offset numBytesFound = Base::_virtualAddressMap.FindMappedMemoryImage(
              dynBase, &dynImage);
          if (numBytesFound < dynSize) {
#if 0
            // It is a regrettably common case that the last thing that
            // looks like the image of a shared library refers to a
            // PT_DYNAMIC section that is not actually in the core.
            // For now, don't complain because it happens pretty much
            // for every core.
            std::cerr << "Only 0x" << std::hex << numBytesFound
                      << " bytes found for PT_DYNAMIC section at 0x"
                      << dynBase << "\n... for image at 0x" << base << "\n";
#endif
            continue;
          }
          int numDyn = dynSize / sizeof(Elf32_Dyn);
          const Elf32_Dyn* dyn = (Elf32_Dyn*)(dynImage);
          const Elf32_Dyn* dynLimit = dyn + numDyn;
          for (; dyn < dynLimit; dyn++) {
            if (dyn->d_tag == DT_STRTAB) {
              dynStrAddr = (Offset)dyn->d_un.d_ptr;
            } else if (dyn->d_tag == DT_SONAME) {
              nameInDynStr = (Offset)dyn->d_un.d_ptr;
            }
          }
        } else {
          if (base != executableAddress) {
            std::cerr << "Library image at 0x" << std::hex << base
                      << " has no PT_DYNAMIC section.\n";
          }
        }
      }
      if (name == (char*)(0)) {
        if (dynStrAddr != 0 && nameInDynStr != 0) {
          Offset numBytesFound = Base::_virtualAddressMap.FindMappedMemoryImage(
              dynStrAddr + nameInDynStr, &name);
          if (numBytesFound < 2) {
            name = (char*)(0);
          }
        }
      }
      if (name == (char*)(0)) {
#if 0
      // This happens for the last image seen in pretty much every core.
      // It also happens for libraries for which the PT_DYNAMIC section
      // does not contain a DT_SONAME image.  This should be fixed at some
      // point but for now it is not a big deal if we can't identify some
      // of the modules.
      std::cerr << "Unable to find name of module at 0x" << std::hex << base
                << "\n";
#endif
      } else {
        Base::_moduleDirectory.AddModule(base, limit - base, name);
      }
    }
  }

  void RefreshSignatureDirectory() const {
    if (Base::_allocationFinder == 0) {
      return;
    }
    if (!_symdefsRead) {
      ReadSymdefsFile();
    }
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
     * Lots of mangled names could start with something else, but typeinfo
     * names are a bit more constrained.
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
          if ((pC[1] != 'b') || ((pC[2] != '0') && (pC[2] != '1')) ||
              (pC[3] != 'E')) {
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
      size_t numToCopy = sizeof(buffer) - 1;
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
        // size_t defEnd = line.find(" in section");
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
