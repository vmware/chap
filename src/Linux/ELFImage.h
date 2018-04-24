// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once

extern "C" {
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
};

#include <functional>
#include "../FileImage.h"
#include "../RangeMapper.h"
#include "../ThreadMap.h"
#include "../VirtualAddressMap.h"

namespace chap {
namespace Linux {
class ElfIOException {};
class FileNotElfException {};
class WrongElfClassException {};
class WrongElfByteOrderException {};
class ElfTruncatedException {};
class NotElfCoreException {};

struct ELF32PRStatusRegInfo {
  static const char *const REGISTER_NAMES[];
  static const size_t REGISTERS_OFFSET = 0x48;
  static const size_t NUM_REGISTERS = 16;  // holes indicated by empty names
  static const size_t STACK_POINTER_INDEX = 15;
};
const char *const ELF32PRStatusRegInfo::REGISTER_NAMES[] = {
    "ebx", "ecx", "edx", "esi", "edi", "ebp", "eax", "",
    "",    "",    "",    "",    "eip", "",    "",    "esp"};
struct ELF64PRStatusRegInfo {
  static const char *const REGISTER_NAMES[];
  static const size_t REGISTERS_OFFSET = 0x70;
  static const size_t NUM_REGISTERS = 22;  // holes indicated by empty name
  static const size_t STACK_POINTER_INDEX = 19;
};
const char *const ELF64PRStatusRegInfo::REGISTER_NAMES[] = {
    "r15", "r14", "r13", "r12", "rbp", "rbx",      "r11", "r10",
    "r9",  "r8",  "rax", "rcx", "rdx", "rsi",      "rdi", "",
    "rip", "",    "",    "rsp", "",    "*fs-base*"};

template <class Ehdr, class Phdr, class Shdr, class Nhdr, class Off, class Word,
          unsigned char elfClass, class PRStatusRegInfo>
class ELFImage {
 public:
  typedef Ehdr ElfHeader;
  typedef Phdr ProgramHeader;
  typedef Shdr SectionHeader;
  typedef Nhdr NoteHeader;
  typedef Off Offset;
  typedef Word ElfWord;
  typedef RangeMapper<Offset, Offset> AddrToOffsetMap;

  static const Offset MAX_OFFSET = ~0;
  ELFImage(const FileImage &fileImage)
      : _fileImage(fileImage),
        _fileSize(fileImage.GetFileSize()),
        _image(fileImage.GetImage()),
        _fileName(fileImage.GetFileName()),
        _virtualAddressMap(fileImage),
        // TODO support notion of cached pthread areas, but not necessarily
        // via ThreadMap because the cached areas have no registers.  We
        // need the mapping due to possible anchors in those areas.
        _threadMap(PRStatusRegInfo::REGISTER_NAMES,
                   PRStatusRegInfo::NUM_REGISTERS),
        _minimumExpectedFileSize(0),
        _isTruncated(false),
        _numThreadsFound(0) {
    if (_fileSize < SELFMAG) {
      throw FileNotElfException();
    }

    if (strncmp((const char *)_image, ELFMAG, SELFMAG)) {
      throw FileNotElfException();
    }

    _elfHeader = (ElfHeader *)_image;

    if (_fileSize < sizeof(ElfHeader)) {
      throw ElfTruncatedException();
    }

    if (((const unsigned char *)_image)[EI_DATA] != ELFDATA2LSB) {
      throw WrongElfByteOrderException();
    }

    if (((const unsigned char *)_image)[EI_CLASS] != elfClass) {
      throw WrongElfClassException();
    }

    int entrySize = _elfHeader->e_phentsize;
    _minimumExpectedFileSize =
        _elfHeader->e_phoff + (_elfHeader->e_phnum * entrySize);
    const char *headerImage = _image + _elfHeader->e_phoff;
    const char *headerLimit = _image + _minimumExpectedFileSize;
    if (_fileSize < _minimumExpectedFileSize) {
      // Some headers are missing from the image.
      if (_fileSize < _elfHeader->e_phoff) {
        headerLimit = headerImage;  // There are no headers in the image.
      } else {
        // Some headers are present.
        headerLimit =
            headerImage +
            ((_fileSize - _elfHeader->e_phoff) / entrySize) * entrySize;
      }
    }

    /*
     * Fill in the virtual address map based on any program headers of
     * type PT_LOAD.
     */

    for (; headerImage < headerLimit; headerImage += entrySize) {
      ProgramHeader *programHeader = (ProgramHeader *)(headerImage);
      if (programHeader->p_type != PT_LOAD) {
        continue;
      }
      Offset sizeInFile = programHeader->p_filesz;
      Offset base = programHeader->p_vaddr;
      Offset size = programHeader->p_memsz;
      Offset adjust = programHeader->p_offset - base;
      Offset flags = programHeader->p_flags;
      if (sizeInFile > 0) {
        Offset limit = programHeader->p_offset + sizeInFile;
        if (size >= sizeInFile) {
          /*
           * The size of the image in the process is at least as large
           * the amount that the program header says was stored in the file.
           * There have been recent cores where just the first page or so
           * of a given virtual address region get mapped and this is
           * reflected in a program header that supplies both the start of
           * the region in the file and the region in the address space but
           * gives a smaller size for the file image.
           */
          if (_fileSize >= limit) {
            /*
             * The entire range that is supposed to be present in
             * the file is there.
             */
            AddRangeToVirtualAddressMap(base, sizeInFile, adjust, true, flags);
          } else if (_fileSize <= programHeader->p_offset) {
            /*
             * None of the range that is supposed to be present actually
             * is present, presumably due to truncation.
             */
            AddRangeToVirtualAddressMap(base, sizeInFile, adjust, false, flags);
          } else {
            /*
             * Only part of the range that is supposed to be present
             * actually is, presumably due to truncation.  Define
             * separate ranges for the part that actually has an image
             * and the part that does not.
             */
            Offset missing = limit - _fileSize;
            Offset present = sizeInFile - missing;
            AddRangeToVirtualAddressMap(base, present, adjust, true, flags);
            AddRangeToVirtualAddressMap(base + present, missing, adjust, false,
                                        flags);
          }
          if (size > sizeInFile) {
            AddRangeToVirtualAddressMap(base + sizeInFile, size - sizeInFile,
                                        adjust, false, flags);
          }
        } else {
          std::cerr << "Warning: a region in the core is larger than the "
                       " mapped range.\n";
        }
        if (_minimumExpectedFileSize < limit) {
          _minimumExpectedFileSize = limit;
        }
      } else {
        /*
         * There is no image of the given region in the file.
         */
        AddRangeToVirtualAddressMap(base, size, adjust, false, flags);
      }
    }

    // TODO: include section headers in calculation of
    // _expectedMinimumFileSize.
    _isTruncated = (_fileSize < _minimumExpectedFileSize);

    if (_elfHeader->e_type == ET_CORE) {
      VisitNotes(
          std::bind(&ELFImage<Ehdr, Phdr, Shdr, Nhdr, Off, Word, elfClass,
                              PRStatusRegInfo>::FindThreadsFromPRStatus,
                    this, std::placeholders::_1, std::placeholders::_2,
                    std::placeholders::_3));
    }
  }

  ~ELFImage() {}

  uint16_t GetELFType() { return _elfHeader->e_type; }

  typedef std::function<bool(ProgramHeader &)> ProgramHeaderVisitor;

  bool VisitProgramHeaders(ProgramHeaderVisitor visitor) {
    int entrySize = _elfHeader->e_phentsize;
    const char *headerImage = _image + _elfHeader->e_phoff;
    const char *headerLimit = headerImage + (_elfHeader->e_phnum * entrySize);
    if (_isTruncated && _image + _fileSize < headerLimit) {
      if (_fileSize < _elfHeader->e_phoff) {
        return false;
      }
      headerLimit = headerImage +
                    ((_fileSize - _elfHeader->e_phoff) / entrySize) * entrySize;
    }
    for (; headerImage < headerLimit; headerImage += entrySize) {
      ProgramHeader *programHeader;
      Offset align = programHeader->p_align;
      if ((align ^ (align - 1)) != ((align << 1) - 1)) {
        /*
         * So far this has only been seen in a fuzzed core, where e_phnum
         * in the ELF header was clobbered, but it is pretty simple to
         * handle it.  There is an annoying tradeoff here in that if the
         * p_align field of a program header gets clobbered, that will cause
         * the remaining program headers to be ignored, but we really don't
         * expect that to happen either.
         */
        break;
      }
      if (visitor(programHeader)) {
        return true;
      }
    }
    return false;
  }

  typedef std::function<bool(std::string &,  // Normalized note name
                             const char *,   // Description
                             ElfWord)        // Note type
                        >
      NoteVisitor;

  bool VisitNotes(NoteVisitor visitor) {
    int entrySize = _elfHeader->e_phentsize;
    const char *headerImage = _image + _elfHeader->e_phoff;
    const char *headerLimit = headerImage + (_elfHeader->e_phnum * entrySize);
    if (_isTruncated && _image + _fileSize < headerLimit) {
      if (_fileSize < _elfHeader->e_phoff) {
        return false;
      }
      headerLimit = headerImage +
                    ((_fileSize - _elfHeader->e_phoff) / entrySize) * entrySize;
    }

    for (; headerImage < headerLimit; headerImage += entrySize) {
      ProgramHeader *programHeader = (ProgramHeader *)headerImage;
      Offset align = programHeader->p_align;
      if ((align ^ (align - 1)) != ((align << 1) - 1)) {
        /*
         * So far this has only been seen in a fuzzed core, where e_phnum
         * in the ELF header was clobbered, but it is pretty simple to
         * handle it.  There is an annoying tradeoff here in that if the
         * p_align field of a program header gets clobbered, that will cause
         * the remaining program headers to be ignored, but we really don't
         * expect that to happen either.
         */
        std::cerr << "Program header at offset 0x" << std::hex
                  << ((char *)programHeader - _image)
                  << " has unexpected alignment 0x" << align
                  << ".\nPerhaps the e_phnum value in the ELF header"
                     " is invalid.\n";
        break;
      }
      if (programHeader->p_type != PT_NOTE) {
        continue;
      }
      if (programHeader->p_offset == 0) {
        std::cerr << "Program header at offset 0x" << std::hex
                  << ((char *)programHeader - _image)
                  << " in process image has invalid p_offset 0.\n";
        break;
      }
      if (_fileSize < (programHeader->p_offset + programHeader->p_filesz)) {
        /*
          The ELF image was truncated and the given section is missing.
          It is not the responsibility of VisitNotes to detect truncation
          but it must avoid crashing in the case of truncation.
        */
        continue;
      }
      const char *noteImage = _image + programHeader->p_offset;
      const char *limit = noteImage + programHeader->p_filesz;
      while (noteImage < limit) {
        NoteHeader *noteHeader = (NoteHeader *)noteImage;
        const char *pName = (const char *)(noteHeader + 1);

        int nameSize = noteHeader->n_namesz;
        if (nameSize > (limit - pName)) {
          std::cerr << "A PT_NOTE section at offset 0x"
                    << programHeader->p_offset
                    << " in the core is not currently parseable.\n";
          break;
        }
        std::string name;
        if (nameSize > 0) {
          /*
           * For most of the cores supported the name is null terminated
           * and the length includes the trailing null byte. For some,
           * the name is not null terminated and the length is just the
           * number of characters.
           */
          if (pName[nameSize - 1] != 0) {
            name.assign(pName, nameSize);
          } else {
            name.assign(pName);
          }
        } else {
          if (nameSize < 0 || (pName + nameSize) > limit) {
            std::cerr << "Warning, an invalid name size was found in "
                      << "the PT_NOTE segment." << std::endl
                      << "Some notes may be skipped." << std::endl;
            return false;
          }
        }
        if (noteHeader->n_descsz == 0) {
          break;
        }
        int descLen = noteHeader->n_descsz;
        const char *pDescription =
            pName + ((nameSize + sizeof(ElfWord) - 1) & ~(sizeof(ElfWord) - 1));

        if (descLen < 0 || (pDescription + descLen > limit)) {
          std::cerr << "Warning, an invalid description size was found in "
                    << "the PT_NOTE segment." << std::endl
                    << "Some notes may be skipped." << std::endl;
          return false;
        }

        if (visitor(name, pDescription, noteHeader->n_type)) {
          return true;
        }
        noteImage = pDescription +
                    ((descLen + sizeof(ElfWord) - 1) & ~(sizeof(ElfWord) - 1));
      }
    }
    return false;
  }

  const FileImage &GetFileImage() { return _fileImage; }
  Offset GetFileSize() const { return _fileSize; }
  Offset GetMinimumExpectedFileSize() const { return _minimumExpectedFileSize; }
  bool IsTruncated() const { return _isTruncated; }

  const VirtualAddressMap<Offset> &GetVirtualAddressMap() const {
    return _virtualAddressMap;
  }

  const ThreadMap<Offset> &GetThreadMap() const { return _threadMap; }

  // TODO: correct access to below fields to private and provide methods to
  // access them as const.  After that VisitNotes and VisitProgramHeaders should
  // become const.

  const FileImage &_fileImage;
  Offset _fileSize;
  const char *_image;
  const std::string &_fileName;
  ElfHeader *_elfHeader;

 private:
  static const Offset OFFSET_SIZE = sizeof(Offset);
  AddrToOffsetMap _rangesWithImages;
  AddrToOffsetMap _rangesMissingImages;
  VirtualAddressMap<Offset> _virtualAddressMap;
  ThreadMap<Offset> _threadMap;
  Offset _minimumExpectedFileSize;
  bool _isTruncated;
  size_t _numThreadsFound;

  void AddRangeToVirtualAddressMap(Offset base, Offset size, Offset adjust,
                                   bool isMapped, Offset flags) {
    _virtualAddressMap.AddRange(base, size, adjust, isMapped,
                                true,                  // has known permissions
                                (flags & PF_R) != 0,   // readable
                                (flags & PF_W) != 0,   // writable
                                (flags & PF_X) != 0);  // executable
  }

  bool FindThreadsFromPRStatus(std::string &noteName, const char *description,
                               ElfWord noteType) {
    if (noteName == "CORE" && noteType == NT_PRSTATUS) {
      size_t threadNum = ++_numThreadsFound;
      Offset *registers =
          ((Offset *)(description + PRStatusRegInfo::REGISTERS_OFFSET));
      Offset stackPointer = registers[PRStatusRegInfo::STACK_POINTER_INDEX];
      typename VirtualAddressMap<Offset>::const_iterator it =
          _virtualAddressMap.find(stackPointer);
      if (it == _virtualAddressMap.end()) {
        std::cerr << "Thread " << std::dec << threadNum
                  << " has unmapped stack top " << std::hex << "0x"
                  << stackPointer << "\n";
      } else if (it.GetImage() == (const char *)(0)) {
        /*
         * The most likely situation is that the core is truncated.  We cannot
         * figure out the stack range for this thread (at least not by the
         * current algorithm) but it is still possible that some of the
         * stacks are present in the core.
         */
        if (!_isTruncated) {
          /*
           * If the core is truncated, the warning about truncation should
           * suffice.  We don't expect the stack image to be missing otherwise
           * but might as well try to handle it.
           */
          std::cerr << "Thread " << std::dec << threadNum
                    << " has no stack image in the core.\n";
        }
      } else {
        /*
         * The base of the range (which limits the growth
         * of the stack because the stack pointer becomes smaller as
         * the stack grows) is pretty reliable on Linux because there
         * is a guard area of intentionally unreadable memory typically
         * placed before the base.  However, there is no such guard area
         * at the other end.  This makes it necessary  to try to guess
         * the limit.
         *
         * The following sequence generally works for pthreads but not,
         * for example, for the main thread.
         */

        typename VirtualAddressMap<Offset>::Reader reader(_virtualAddressMap);
        Offset maxLimit = it.Limit();
        Offset maxSelfRef = maxLimit - 3 * OFFSET_SIZE;
        Offset limit = maxLimit;
        for (Offset selfRef = (stackPointer + OFFSET_SIZE) & ~(OFFSET_SIZE - 1);
             selfRef <= maxSelfRef; selfRef += OFFSET_SIZE) {
          if ((reader.ReadOffset(selfRef) == selfRef) &&
              (reader.ReadOffset(selfRef + 2 * OFFSET_SIZE) == selfRef)) {
            limit = ((selfRef + 0x1000) & ~0xFFF);
            break;
          }
        }
        _threadMap.AddThread(it.Base(), stackPointer, limit, registers,
                             threadNum);
      }
    }
    return false;
  }
};

typedef ELFImage<Elf32_Ehdr, Elf32_Phdr, Elf32_Shdr, Elf32_Nhdr, Elf32_Off,
                 Elf32_Word, ELFCLASS32, ELF32PRStatusRegInfo>
    Elf32;

typedef ELFImage<Elf64_Ehdr, Elf64_Phdr, Elf64_Shdr, Elf64_Nhdr, Elf64_Off,
                 Elf64_Word, ELFCLASS64, ELF64PRStatusRegInfo>
    Elf64;

}  // namespace Linux
}  // namespace chap
