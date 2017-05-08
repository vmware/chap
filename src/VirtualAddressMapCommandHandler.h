// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <vector>
#include "Commands/Runner.h"
#include "VirtualAddressMap.h"
namespace chap {
template <typename Offset>
class VirtualAddressMapCommandHandler {
 public:
  typedef VirtualAddressMap<Offset> AddressMap;
  VirtualAddressMapCommandHandler(const AddressMap& addressMap)
      : _addressMap(addressMap) {}

  size_t DumpAddressRange(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    Offset startAddr;
    Offset numBytes;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "dump") {
      numTokensAccepted++;
      if (context.ParseTokenAt(1, startAddr)) {
        numTokensAccepted++;
        if (context.ParseTokenAt(2, numBytes)) {
          numTokensAccepted++;
        }
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 3) {
        error << "Usage: dump <addr-in-hex> <size-in-hex>\n";
      } else {
        const char* image;
        Offset numBytesFound =
            _addressMap.FindMappedMemoryImage(startAddr, &image);
        if (numBytesFound < numBytes) {
          error << "Only 0x" << std::hex << numBytesFound
                << " bytes were mapped starting from that address\n";
          numBytes = numBytesFound;
        }
        output.HexDump((const Offset*)image, numBytes);
      }
    }
    return numTokensAccepted;
  }

  size_t StringAt(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    Offset startAddr;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "string") {
      numTokensAccepted++;
      if (context.ParseTokenAt(1, startAddr)) {
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 2) {
        error << "Usage: string <addr-in-hex>\n";
      } else {
        const char* image;
        Offset numBytesFound =
            _addressMap.FindMappedMemoryImage(startAddr, &image);
        size_t length = 0;
        for ( ; length < numBytesFound; ++length) {
          char c = image[length];
          if (c < ' ' || c > 0x7e) {
            break;
          }
        }
        // TODO - make more robust (for other characters ...)
        // TODO - consider making part of Commands::output
        output << "\"" << std::string(image, length)  << "\"\n";
      }
    }
    return numTokensAccepted;
  }

  size_t WideStringAt(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    Offset startAddr;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "wstring") {
      numTokensAccepted++;
      if (context.ParseTokenAt(1, startAddr)) {
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 2) {
        error << "Usage: string <addr-in-hex>\n";
      } else {
        const char* image;
        Offset numBytesFound =
            _addressMap.FindMappedMemoryImage(startAddr, &image);
        // TODO - make more robust (for other characters ...)
        // TODO - consider making part of Commands::output
        output << "\"";
        for (Offset i = 0; i < numBytesFound; ++i) {
          if (image[i << 1] == 0) {
            break;
          }
          if (image[(i << 1) + 1] == 0) {
            output << image[i << 1];
          } else {
            // TODO: escape this suitably.
            output << '?';
          }
        }
        output << "\"\n";
      }
    }
    return numTokensAccepted;
  }

  size_t FindPointer(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    output << std::hex;
    Offset valueToMatch;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "findptr") {
      numTokensAccepted++;
      if (context.ParseTokenAt(1, valueToMatch)) {
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      Commands::Output& output = context.GetOutput();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 2) {
        error << "Usage: findptr <addr-in-hex>\n";
      } else {
        typename AddressMap::const_iterator itEnd = _addressMap.end();
        for (typename AddressMap::const_iterator it = _addressMap.begin();
             it != itEnd; ++it) {
          Offset numCandidates = it.Size() / sizeof(Offset);
          const char* rangeImage = it.GetImage();
          if (rangeImage != (const char*)0) {
            const Offset* nextCandidate = (const Offset*)(rangeImage);

            for (const Offset* limit = nextCandidate + numCandidates;
                 nextCandidate < limit; nextCandidate++) {
              if (*nextCandidate == valueToMatch) {
                output << ((it.Base()) +
                           ((const char*)nextCandidate - rangeImage))
                       << "\n";
              }
            }
          }
        }
      }
    }
    return numTokensAccepted;
  }

  size_t FindRelRef(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    output << std::hex;
    Offset valueToMatch;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "findrelref") {
      numTokensAccepted++;
      if (context.ParseTokenAt(1, valueToMatch)) {
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      Commands::Output& output = context.GetOutput();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 2) {
        error << "Usage: findptr <addr-in-hex>\n";
      } else {
        typename AddressMap::const_iterator itEnd = _addressMap.end();
        for (typename AddressMap::const_iterator it = _addressMap.begin();
             it != itEnd; ++it) {
          const char* rangeImage = it.GetImage();
          if (rangeImage != (const char*)0) {
            const unsigned char* nextCandidate =
                (const unsigned char*)(rangeImage);

            Offset addr = it.Base();
            for (const unsigned char* limit =
                     nextCandidate + it.Size() - sizeof(int) + 1;
                 nextCandidate < limit; nextCandidate++) {
              if (addr + sizeof(int) + *(int*)(nextCandidate) == valueToMatch) {
                output << std::hex << addr << "\n";
              }
              addr++;
            }
          }
        }
      }
    }
    return numTokensAccepted;
  }

  size_t FindUint32(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    output << std::hex;
    Offset valueToMatch;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "find32") {
      numTokensAccepted++;
      if (context.ParseTokenAt(1, valueToMatch)) {
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      Commands::Output& output = context.GetOutput();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 2) {
        error << "Usage: find32 <addr-in-hex>\n";
      } else {
        uint32_t value = (uint32_t)valueToMatch;
        typename AddressMap::const_iterator itEnd = _addressMap.end();
        for (typename AddressMap::const_iterator it = _addressMap.begin();
             it != itEnd; ++it) {
          Offset numCandidates = it.Size() / sizeof(uint32_t);
          const char* rangeImage = it.GetImage();
          if (rangeImage != (const char*)0) {
            const uint32_t* nextCandidate = (const uint32_t*)(rangeImage);

            for (const uint32_t* limit = nextCandidate + numCandidates;
                 nextCandidate < limit; nextCandidate++) {
              if (*nextCandidate == value) {
                output << ((it.Base()) +
                           ((const char*)nextCandidate - rangeImage))
                       << "\n";
              }
            }
          }
        }
      }
    }
    return numTokensAccepted;
  }

  size_t FindBytes(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    output << std::hex;
    Offset valueToMatch;
    size_t numTokensAccepted = 0;
    std::vector<unsigned char> bytes;
    if (context.TokenAt(0) == "findbytes") {
      numTokensAccepted++;
      while (context.ParseTokenAt(numTokensAccepted, valueToMatch) &&
             valueToMatch <= 0xff) {
        if (!checkOnly) {
          bytes.push_back((unsigned char)(valueToMatch));
        }
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      Commands::Output& output = context.GetOutput();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted < 2) {
        error << "Usage: findbytes <v1> [<v2>...<vn>]\n";
      } else {
        size_t numBytes = bytes.size();
        typename AddressMap::const_iterator itEnd = _addressMap.end();
        for (typename AddressMap::const_iterator it = _addressMap.begin();
             it != itEnd; ++it) {
          const unsigned char* rangeImage = (unsigned char*)it.GetImage();
          if (rangeImage != (const unsigned char*)0) {
            const unsigned char* rangeLimit = rangeImage + it.Size();
            const unsigned char* nextCandidate = rangeImage;

            for (const unsigned char* limit = rangeLimit - numBytes + 1;
                 nextCandidate < limit; nextCandidate++) {
              for (size_t i = 0; nextCandidate[i] == bytes[i];) {
                if (++i == numBytes) {
                  output << ((it.Base()) + (nextCandidate - rangeImage))
                         << "\n";
                }
              }
            }
          }
        }
      }
    }
    return numTokensAccepted;
  }

  virtual void AddCommandCallbacks(Commands::Runner& r) {
    r.AddCommand("dump",
                 std::bind(&VirtualAddressMapCommandHandler::DumpAddressRange,
                           this, std::placeholders::_1, std::placeholders::_2));
    r.AddCommand("findptr",
                 std::bind(&VirtualAddressMapCommandHandler::FindPointer, this,
                           std::placeholders::_1, std::placeholders::_2));
    r.AddCommand("findrelref",
                 std::bind(&VirtualAddressMapCommandHandler::FindRelRef, this,
                           std::placeholders::_1, std::placeholders::_2));
    r.AddCommand("findbytes",
                 std::bind(&VirtualAddressMapCommandHandler::FindBytes, this,
                           std::placeholders::_1, std::placeholders::_2));
    r.AddCommand("find32",
                 std::bind(&VirtualAddressMapCommandHandler::FindUint32, this,
                           std::placeholders::_1, std::placeholders::_2));
    r.AddCommand("string",
                 std::bind(&VirtualAddressMapCommandHandler::StringAt, this,
                           std::placeholders::_1, std::placeholders::_2));
    r.AddCommand("wstring",
                 std::bind(&VirtualAddressMapCommandHandler::WideStringAt, this,
                           std::placeholders::_1, std::placeholders::_2));
  }

 private:
  const VirtualAddressMap<Offset>& _addressMap;
};
}  // namespace chap
