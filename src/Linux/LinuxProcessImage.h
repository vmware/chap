// Copyright (c) 2017-2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include <map>
#include <regex>
#include "../Allocations/TaggerRunner.h"
#include "../CPlusPlus/Unmangler.h"
#include "../LibcMalloc/FinderGroup.h"
#include "../ProcessImage.h"
#include "ELFImage.h"
#include "ELFModuleImageFactory.h"
#include "ModuleFinder.h"

namespace chap {
namespace Linux {
template <class ElfImage>
class LinuxProcessImage : public ProcessImage<typename ElfImage::Offset> {
 public:
  typedef typename ElfImage::Offset Offset;
  typedef ProcessImage<Offset> Base;
  typedef VirtualAddressMap<Offset> AddressMap;
  typedef typename AddressMap::Reader Reader;
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  typedef typename Allocations::SignatureDirectory<Offset> SignatureDirectory;
  LinuxProcessImage(ElfImage& elfImage, bool truncationCheckOnly)
      : ProcessImage<Offset>(elfImage.GetVirtualAddressMap(),
                             elfImage.GetThreadMap(),
                             new ELFModuleImageFactory<ElfImage>()),
        _elfImage(elfImage),
        _firstReadableStackGuardFound(false),
        _symdefsRead(false) {
    if (_elfImage.GetELFType() != ET_CORE) {
      /*
       * It is the responsibilty of the caller to avoid passing in an ELFImage
       * that corresponds to something other than a core.
       */

      abort();
    }

    if (truncationCheckOnly) {
      return;
    }

    FindFileMappedRanges();

    FindModules();

    /*
     * This finds the large structures associated with libc malloc then
     * registers any relevant allocation finders with the allocation
     * directory.
     */

    _libcMallocFinderGroup.reset(new LibcMalloc::FinderGroup<Offset>(
        Base::_virtualMemoryPartition, Base::_moduleDirectory,
        Base::_allocationDirectory, Base::_threadMap, Base::_unfilledImages));

    Base::_pythonFinderGroup.Resolve();
    Base::_goLangFinderGroup.Resolve();
    Base::_pThreadInfrastructureFinder.Resolve();
    Base::_follyFibersInfrastructureFinder.Resolve();

    /*
     * At this point we should have identified all the stacks except the one
     * used for the main thread.  This means we can look through the thread
     * map and associated any threads with stacks by identifying the stack
     * that uses the stack pointer for each thread.  The stack pointer that
     * is not associated with any stack that has been found yet must belong
     * to the main thread, unless the core contains stacks of some kind not
     * yet recognized.
     */
    std::vector<std::pair<Offset, size_t> > mainStackCandidates;
    mainStackCandidates.reserve(3);
    for (typename ThreadMap<Offset>::const_iterator it =
             Base::_threadMap.begin();
         it != Base::_threadMap.end(); ++it) {
      if (!Base::_stackRegistry.AddThreadNumber(it->_stackPointer,
                                                it->_threadNum)) {
        mainStackCandidates.emplace_back(it->_stackPointer, it->_threadNum);
      }
    }
    size_t numMainStackCandidates = mainStackCandidates.size();
    if (numMainStackCandidates == 1) {
      const auto mainStackPointerAndThread = mainStackCandidates[0];
      if (!RegisterMainStack(mainStackPointerAndThread.first,
                             mainStackPointerAndThread.second)) {
        std::cerr
            << "Leak information cannot be trusted without the main stack.\n";
      }
    } else {
      if (numMainStackCandidates == 0) {
        if (!elfImage.IsTruncated()) {
          std::cerr << "Warning: No thread appears to be using the original "
                       "stack for the main thread.\n";
        }
      } else {
        std::cerr << "Warning: There are multiple candidates to be the main "
                     "stack,\nincluding the following:\n";
        for (const auto& spAndThread : mainStackCandidates) {
          std::cerr << "Stack with stack pointer 0x" << std::hex
                    << spAndThread.first << " used by thread " << std::dec
                    << spAndThread.second << "\n";
        }
      }
    }

    /*
     * Now that any allocation finders have been registered with the
     * allocaion directory, find out where all the allocations are.
     */
    Base::_allocationDirectory.ResolveAllocationBoundaries();

    /*
     * Finding statically declared type_info structures depends on
     * finding the modules first.  Associating these type_info ranges with
     * signatures used by allocations depends on finding the allocations
     * first.
     */
    Base::_typeInfoDirectory.Resolve();

    /*
     * Static anchor ranges should be found after the allocations and modules,
     * both because both the writable regions for modules and all imaged
     * writable
     * memory is considered to be OK for anchors.  This is sometimes
     * inaccurate,
     * because mmapped memory not allocated by a known allocator is considered
     * as
     * anchors, but it is necessary to consider the unknown regions to be
     * anchors
     * to avoid false leaks.
     */
    FindStaticAnchorRanges();

    Base::_allocationGraph = new Allocations::Graph<Offset>(
        Base::_virtualAddressMap, Base::_allocationDirectory, Base::_threadMap,
        Base::_stackRegistry, _staticAnchorLimits, nullptr, nullptr);

    /*
     * In Linux processes the current approach is to wait until the
     * allocations have been found, then treat pointers at the start of
     * the allocations to read only memory as signatures.  This means
     * that the signatures can't be identified until the allocations have
     * been found.
     */

    FindSignaturesInAllocations();

    FindSignatureNamesFromBinaries();

    WriteSymreqsFileIfNeeded();

    /*
     * Once this constructor as finished, any classification of ranges is
     * done.
     */
    Base::_virtualMemoryPartition.ClaimUnclaimedRangesAsUnknown();

    Base::TagAllocations();
  }

  LibcMalloc::FinderGroup<Offset>& GetLibcMallocFinderGroup() const {
    return *(_libcMallocFinderGroup.get());
  }

  void RefreshSignaturesAndAnchors() {
    if (!_symdefsRead) {
      ReadSymdefsFile();
    }
  }

 private:
  std::unique_ptr<LibcMalloc::FinderGroup<Offset> > _libcMallocFinderGroup;

  void FindModules() {
    ModuleFinder<ElfImage> moduleFinder(Base::_virtualMemoryPartition,
                                        Base::_fileMappedRangeDirectory,
                                        Base::_moduleDirectory);
    moduleFinder.FindModules();
  }

  bool ProcessELIFNote(std::string& noteName, const char* description,
                       typename ElfImage::ElfWord noteType) {
    if (noteName == "CORE" && noteType == 0x46494c45) {
      /* 0x46494c45 is "FILE" backward in little-endian, so "ELIF" */
      Offset numMappedRanges = *((const Offset*)(description));
      const Offset* firstMappedRange = ((const Offset*)(description)) + 2;
      const Offset* pastMappedRanges = firstMappedRange + 3 * numMappedRanges;
      Offset fileOffsetMultiplier = 1;
      for (const Offset* mappedRange = firstMappedRange;
           mappedRange < pastMappedRanges; mappedRange += 3) {
        if ((mappedRange[2] & 0xfff) != 0) {
          fileOffsetMultiplier = 0x1000;
          break;
        }
      }
      const char* nextRangePath = (const char*)pastMappedRanges;
      for (const Offset* mappedRange = firstMappedRange;
           mappedRange < pastMappedRanges; mappedRange += 3) {
        Offset rangeBase = mappedRange[0];
        Offset rangeLimit = mappedRange[1];
        Offset offsetInFile = mappedRange[2] * fileOffsetMultiplier;
        const char* rangePath = nextRangePath;
        nextRangePath += strlen(nextRangePath) + 1;

        typename VirtualAddressMap<Offset>::const_iterator it =
            Base::_virtualAddressMap.upper_bound(rangeBase);
        if ((it == Base::_virtualAddressMap.end()) ||
            (it.Base() >= rangeLimit)) {
          /*
           * We don't know the flags at this point because none  of the mapped
           * range is actually present in the core.
           */
          Base::_fileMappedRangeDirectory.AddRange(
              rangeBase, rangeLimit - rangeBase, rangePath, offsetInFile, 0);
          continue;
        }

        /*
         * At least part of the range given in the ELIF note is also known
         * in the PT_LOAD section.  In theory, the whole range should be 
         * known, because even if the coredump_filter effectively specifies
         * that certain regions should be omitted, they should still appear
         * in the PT_LOAD section but not mapped in the core, but this aspect
         * of core generation has been broken for years.  A way to avoid this
         * is for the minimum bits set in the coredump_filter to be the
         * ones in 0x37.  Once we figure out the flags for part of the range
         * we roughly know the flags for the entire range, where for any
         * part of the range not known to the core, that range is also
         * definitely not mapped.
         */
        int flags = it.Flags();
        int flagsWithoutMapping = flags & ~RangeAttributes::IS_MAPPED;
        Offset virtualAddressMapRangeBase = it.Base();

        while (true) {
          if (virtualAddressMapRangeBase > rangeBase) {
            Base::_fileMappedRangeDirectory.AddRange(
                rangeBase, virtualAddressMapRangeBase - rangeBase, rangePath,
                offsetInFile, flagsWithoutMapping);
            offsetInFile += virtualAddressMapRangeBase - rangeBase;
            rangeBase = virtualAddressMapRangeBase;
          }
          Offset mappedLimit = it.Limit();
          if (mappedLimit > rangeLimit) {
            mappedLimit = rangeLimit;
          }
          Base::_fileMappedRangeDirectory.AddRange(
              rangeBase, mappedLimit - rangeBase, rangePath, offsetInFile,
              flags);
          offsetInFile += mappedLimit - rangeBase;
          rangeBase = mappedLimit;
          if (rangeBase == rangeLimit) {
            break;
          }
          if (++it == Base::_virtualAddressMap.end()) {
            break;
          }
          virtualAddressMapRangeBase = it.Base();
          if (virtualAddressMapRangeBase >= rangeLimit) {
            break;
          }
        }
        if (rangeBase < rangeLimit) {
          Base::_fileMappedRangeDirectory.AddRange(
              rangeBase, rangeLimit - rangeBase, rangePath,
              offsetInFile, flagsWithoutMapping);
        }
      }
    }
    return false;
  }

  void FindFileMappedRanges() {
    (void)_elfImage.VisitNotes(std::bind(
        &LinuxProcessImage<ElfImage>::ProcessELIFNote, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  }

  void WarnIfFirstReadableStackGuardFound() {
    if (!_firstReadableStackGuardFound) {
      _firstReadableStackGuardFound = true;
      std::cerr
          << "Warning: At least one readable stack guard has been found.\n"
             " This generally means that the gdb code that created the core "
             "has a bug\n"
             " and that the permissions were marked wrong in the core.\n";
    }
  }

  bool RegisterMainStack(Offset stackPointer, size_t threadNumber) {
    const char* stackType = "main stack";
    typename VirtualAddressMap<Offset>::const_iterator it =
        Base::_virtualAddressMap.find(stackPointer);
    if (it == Base::_virtualAddressMap.end()) {
      std::cerr << "Process image does not contain mapping for " << stackType
                << " that contains address 0x" << std::hex << stackPointer
                << ".\n";
      return false;
    }
    if (it.GetImage() == (const char*)(0)) {
      std::cerr << "Process image does not contain image for " << stackType
                << " that contains address 0x" << std::hex << stackPointer
                << ".\n";
      return false;
    }
    Offset base = it.Base();
    Offset limit = it.Limit();
    /*
     * TODO: Derive end of main stack rather than guessing the limits.
     */
    if (!Base::_virtualMemoryPartition.ClaimRange(base, limit - base, stackType,
                                                  false)) {
      std::cerr << "Warning: Failed to claim " << stackType << " [" << std::hex
                << base << ", " << limit << ") due to overlap.\n";
      return false;
    }
    if (!Base::_stackRegistry.RegisterStack(base, limit, stackType)) {
      std::cerr << "Warning: Failed to register " << stackType << " ["
                << std::hex << base << ", " << limit
                << ") due to overlap with other stack.\n";
      return false;
    }
    if (!Base::_stackRegistry.AddThreadNumber(stackPointer, threadNumber)) {
      std::cerr
          << "Warning: Can't associate main stack with main thread number.\n";
      return false;
    }
    return true;
  }

 private:
  ElfImage& _elfImage;
  bool _firstReadableStackGuardFound;
  bool _symdefsRead;
  std::map<Offset, Offset> _staticAnchorLimits;

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
  std::string CopyAndUnmangle(
      const VirtualAddressMap<Offset>& virtualAddressMap,
      Offset mangledNameAddr) const {
    std::string unmangledName;
    Reader reader(virtualAddressMap);
    char buffer[1000];
    size_t numCopied =
        reader.ReadCString(mangledNameAddr, buffer, sizeof(buffer));
    if (numCopied != 0 && numCopied != sizeof(buffer)) {
      CPlusPlus::Unmangler<Offset> unmangler(buffer, false);
      unmangledName = unmangler.Unmangled();
    }
    return unmangledName;
  }

  std::string GetUnmangledTypeinfoName(
      const VirtualAddressMap<Offset>& virtualAddressMap,
      Offset signature) const {
    std::string emptySignatureName;
    Offset typeInfoPointerAddress = signature - sizeof(Offset);

    Reader reader(virtualAddressMap);
    Offset typeInfoAddress = reader.ReadOffset(typeInfoPointerAddress, 0);
    if (typeInfoAddress == 0) {
      return emptySignatureName;
    }
    Offset typeInfoNameAddress =
        reader.ReadOffset(typeInfoAddress + sizeof(Offset), 0);
    if (typeInfoNameAddress != 0) {
      typename VirtualAddressMap<Offset>::const_iterator it =
          Base::_virtualAddressMap.find(typeInfoNameAddress);
      if ((it != Base::_virtualAddressMap.end()) &&
          ((it.Flags() & RangeAttributes::IS_WRITABLE) == 0)) {
        return CopyAndUnmangle(virtualAddressMap, typeInfoNameAddress);
      }
    }
    return emptySignatureName;
  }

  bool ReadSymdefsFile() {
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
        if (signature != 0) {
          Base::_signatureDirectory.MapSignatureNameAndStatus(
              signature, "",
              SignatureDirectory::UNWRITABLE_MISSING_FROM_SYMDEFS);
          signature = 0;
        }
        anchor = 0;
      }

      if (signature != 0) {
        size_t defStart = 0;
        size_t forPos = line.find(" for ");
        bool isVTable = (forPos != std::string::npos);
        if (isVTable) {
          defStart = forPos + 5;
        }
        size_t defEnd = line.find(" in section");
        size_t plusPos = line.find(" + ");
        defEnd = plusPos;
        std::string name(line.substr(defStart, defEnd - defStart));

        Base::_signatureDirectory.MapSignatureNameAndStatus(
            signature, name,
            isVTable ? SignatureDirectory::VTABLE_WITH_NAME_FROM_SYMDEFS
                     : SignatureDirectory::UNWRITABLE_WITH_NAME_FROM_SYMDEFS);
        signature = 0;
      } else if (anchor != 0) {
        size_t defEnd = line.find(" in section");
        std::string name(line.substr(0, defEnd));
        Base::_anchorDirectory.MapAnchorToName(anchor, name);
        anchor = 0;
      }
    }
    symDefs.close();
    _symdefsRead = true;
    return true;
  }

  /*
   * Initialize the signature directory to contain an entry for each
   * read-only address seen in the pointer at the start of each allocation
   * that is aligned on a pointer-sized boundary.
   */

  void FindSignaturesInAllocations() {
    const Allocations::Directory<Offset>& directory =
        Base::_allocationDirectory;
    typename Allocations::Directory<Offset>::AllocationIndex numAllocations =
        directory.NumAllocations();
    Reader reader(Base::_virtualAddressMap);
    typename VirtualAddressMap<Offset>::const_iterator itEnd =
        Base::_virtualAddressMap.end();
    for (typename Allocations::Directory<Offset>::AllocationIndex i = 0;
         i < numAllocations; ++i) {
      const typename Allocations::Directory<Offset>::Allocation* allocation =
          directory.AllocationAt(i);
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

      bool writableVtable = false;
      typename Allocations::SignatureDirectory<Offset>::Status status =
          SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS;
      if ((it.Flags() & RangeAttributes::IS_WRITABLE) != 0) {
        /*
         * Some recent linkers end up causing vtables to be writable
         * at times.  This is a security bug, but we want chap to
         * support such signatures.  For now they are supported only
         * if the mangled name is actually in the core.  In the case that
         * the vtable is writable, it may be in the static area associated
         * with a module or if not it will be in an area of memory that is not
         * yet analyzed by chap.
         */
        if (Base::_virtualMemoryPartition.IsClaimed(signature)) {
          Offset relativeSignature;
          Offset rangeBase = 0;
          Offset rangeSize = 0;
          std::string newModulePath;
          if (!Base::_moduleDirectory.Find(signature, newModulePath, rangeBase,
                                           rangeSize, relativeSignature)) {
            /*
             * If the signature points to a claimed region, we expect it to
             * refer to a module, as opposed to, for example, dynamically
             * allocated memory.
             */
            continue;
          }
          Offset typeinfoAddr =
              reader.ReadOffset(signature - sizeof(Offset), 0xbadbad);
          if (typeinfoAddr == 0xbadbad) {
            /*
             * If the typeinfo is not in the process image, perhaps the
             * signature does not point to a vtable.  At any rate, excluding
             * this case is needed to avoid false signatures.
             */
            continue;
          }
          Offset toVtableStart =
              reader.ReadOffset(signature - 2 * sizeof(Offset), 0xbadbad);
          if (toVtableStart != 0 &&
              (toVtableStart >= 0x10000 ||
               reader.ReadOffset(signature - 2 * sizeof(Offset) - toVtableStart,
                                 0xbadbad) != 0)) {
            /*
             * Just before the pointer to the typeinfo there should be an offset
             * from that location to the start of the vtable, which always has
             * a 0.
             */
            continue;
          }
          if (!Base::_moduleDirectory.Find(typeinfoAddr, newModulePath,
                                           rangeBase, rangeSize,
                                           relativeSignature)) {
            /*
             * Again to avoid false signatures in this case, we insist that the
             * typeinfo is associated with a module.
             */
            continue;
          }
          status = SignatureDirectory::WRITABLE_MODULE_REFERENCE;
        }
        writableVtable = true;
      }

      std::string typeinfoName =
          GetUnmangledTypeinfoName(Base::_virtualAddressMap, signature);
      if (writableVtable) {
        if (typeinfoName.empty()) {
          /*
           * We were guessing that this was possibly a writable vtable
           * pointer, but didn't actually reach a mangled type name.
           */
          if (status != SignatureDirectory::WRITABLE_MODULE_REFERENCE) {
            /*
             * In the case that both the signature and the possible typeinfo
             * pointers were to modules, we should be willing to try for
             * this as a signature via symreqs/symdefs.  If not, give up.
             */
            continue;
          }
        } else {
          std::cerr << "Warning: type " << typeinfoName
                    << " has a writable vtable at 0x" << std::hex << signature
                    << ".\n";
          std::cerr << "... This is a security violation.\n";
          status =
              SignatureDirectory::WRITABLE_VTABLE_WITH_NAME_FROM_PROCESS_IMAGE;
        }
      } else {
        if (!typeinfoName.empty()) {
          status = SignatureDirectory::VTABLE_WITH_NAME_FROM_PROCESS_IMAGE;
        }
      }
      Base::_signatureDirectory.MapSignatureNameAndStatus(signature,
                                                          typeinfoName, status);
    }
  }

  void FindSignatureNamesFromBinaries() {
    std::string modulePath;
    std::unique_ptr<FileImage> fileImage;
    std::unique_ptr<ElfImage> elfImage;
    Reader reader(Base::_virtualAddressMap);
    typename SignatureDirectory::SignatureNameAndStatusConstIterator itEnd =
        Base::_signatureDirectory.EndSignatures();
    for (typename SignatureDirectory::SignatureNameAndStatusConstIterator it =
             Base::_signatureDirectory.BeginSignatures();
         it != itEnd; ++it) {
      typename SignatureDirectory::Status status = it->second.second;
      if (status != SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS &&
          status != SignatureDirectory::WRITABLE_MODULE_REFERENCE) {
        continue;
      }
      Offset signature = it->first;
      Offset relativeSignature;
      Offset rangeBase = 0;
      Offset rangeSize = 0;
      std::string newModulePath;
      if (!Base::_moduleDirectory.Find(signature, newModulePath, rangeBase,
                                       rangeSize, relativeSignature)) {
        continue;
      }
      if (newModulePath != modulePath) {
        modulePath = newModulePath;
        try {
          fileImage.reset(new FileImage(modulePath.c_str(), false));
          elfImage.reset(new ElfImage(*fileImage));
        } catch (...) {
          elfImage.reset(0);
          continue;
        }
      }
      if (elfImage == 0) {
        continue;
      }
      const VirtualAddressMap<Offset>& virtualAddressMap =
          elfImage->GetVirtualAddressMap();
      std::string typeinfoName;
      typeinfoName =
          GetUnmangledTypeinfoName(virtualAddressMap, relativeSignature);
      if (typeinfoName.empty()) {
        Offset typeinfoAddr = reader.ReadOffset(signature - sizeof(Offset), 0);
        if (typeinfoAddr == 0) {
          continue;
        }
        Offset mangledNameAddr =
            reader.ReadOffset(typeinfoAddr + sizeof(Offset), 0);
        if (mangledNameAddr == 0) {
          continue;
        }
        Offset relativeNameAddr;
        if (!Base::_moduleDirectory.Find(mangledNameAddr, newModulePath,
                                         rangeBase, rangeSize,
                                         relativeNameAddr)) {
          continue;
        }
        if (newModulePath == modulePath) {
          typeinfoName = CopyAndUnmangle(virtualAddressMap, relativeNameAddr);
        } else {
          try {
            std::unique_ptr<FileImage> fileImageForName(
                new FileImage(newModulePath.c_str(), false));
            std::unique_ptr<ElfImage> elfImageForName(
                new ElfImage(*fileImageForName));
            typeinfoName = CopyAndUnmangle(
                elfImageForName->GetVirtualAddressMap(), relativeNameAddr);
          } catch (...) {
          }
        }
      }
      if (!typeinfoName.empty()) {
        Base::_signatureDirectory.MapSignatureNameAndStatus(
            signature, typeinfoName,
            SignatureDirectory::VTABLE_WITH_NAME_FROM_BINARY);
      }
    }
  }

  void AddSignatureRequestsToSymReqs(std::ofstream& gdbScriptFile) {
    typename SignatureDirectory::SignatureNameAndStatusConstIterator itEnd =
        Base::_signatureDirectory.EndSignatures();
    for (typename SignatureDirectory::SignatureNameAndStatusConstIterator it =
             Base::_signatureDirectory.BeginSignatures();
         it != itEnd; ++it) {
      Offset signature = it->first;
      typename SignatureDirectory::Status status = it->second.second;
      if (status == SignatureDirectory::UNWRITABLE_PENDING_SYMDEFS ||
          status == SignatureDirectory::WRITABLE_MODULE_REFERENCE) {
        gdbScriptFile << "printf \"SIGNATURE " << std::hex << signature
                      << "\\n\"" << '\n'
                      << "info symbol 0x" << signature << '\n';
      }
    }
  }

  void AddAnchorRequestsToSymReqs(std::ofstream& gdbScriptFile) {
    const Allocations::Graph<Offset>& graph = *(Base::_allocationGraph);
    const Allocations::Directory<Offset>& directory =
        Base::_allocationDirectory;
    typename Allocations::Directory<Offset>::AllocationIndex numAllocations =
        directory.NumAllocations();
    for (typename Allocations::Directory<Offset>::AllocationIndex i = 0;
         i < numAllocations; ++i) {
      const typename Allocations::Directory<Offset>::Allocation* allocation =
          directory.AllocationAt(i);
      if (!allocation->IsUsed() || !graph.IsStaticAnchorPoint(i)) {
        continue;
      }
      const std::vector<Offset>* anchors = graph.GetStaticAnchors(i);
      typename std::vector<Offset>::const_iterator itAnchorsEnd =
          anchors->end();
      for (typename std::vector<Offset>::const_iterator itAnchors =
               anchors->begin();
           itAnchors != itAnchorsEnd; ++itAnchors) {
        gdbScriptFile << "printf \"ANCHOR " << std::hex << *itAnchors << "\\n\""
                      << '\n'
                      << "info symbol 0x" << std::hex << *itAnchors << '\n';
      }
    }
  }
  void WriteSymreqsFileIfNeeded() {
    std::string symReqsPath(
        Base::_virtualAddressMap.GetFileImage().GetFileName());
    symReqsPath.append(".symreqs");
    std::ifstream symReqs;
    symReqs.open(symReqsPath.c_str());
    if (!symReqs.fail()) {
      return;
    }

    std::ofstream gdbScriptFile;
    gdbScriptFile.open(symReqsPath.c_str());
    if (gdbScriptFile.fail()) {
      std::cerr << "Unable to open " << symReqsPath << " for writing.\n";
      return;
    }

    std::string symDefsPath(
        Base::_virtualAddressMap.GetFileImage().GetFileName());
    symDefsPath.append(".symdefs");

    gdbScriptFile << "set logging file " << symDefsPath << '\n';
    gdbScriptFile << "set logging overwrite 1\n";
    gdbScriptFile << "set logging redirect 1\n";
    gdbScriptFile << "set logging on\n";
    gdbScriptFile << "set height 0\n";
    AddSignatureRequestsToSymReqs(gdbScriptFile);
    AddAnchorRequestsToSymReqs(gdbScriptFile);
    gdbScriptFile << "set logging off\n";
    gdbScriptFile << "set logging overwrite 0\n";
    gdbScriptFile << "set logging redirect 0\n";
    gdbScriptFile << "printf \"output written to " << symDefsPath << "\\n\""
                  << '\n';
    gdbScriptFile.close();
  }

  void FindStaticAnchorRanges() {
    for (const auto& range :
         Base::_virtualMemoryPartition.GetStaticAnchorCandidates()) {
      _staticAnchorLimits[range._base] = range._limit;
    }
  }
};
}  // namespace Linux
}  // namespace chap
