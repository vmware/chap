// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class KnownAddressDescriber : public Describer<Offset> {
  typedef typename VirtualAddressMap<Offset>::RangeAttributes Attributes;

 public:
  KnownAddressDescriber(const ProcessImage<Offset> *processImage) {
    SetProcessImage(processImage);
  }

  void SetProcessImage(const ProcessImage<Offset> *processImage) {
    if (processImage == 0) {
      _virtualAddressMap = 0;
    } else {
      _virtualAddressMap = &(processImage->GetVirtualAddressMap());
    }
  }

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  This particular describer is intended as basically
   * a last resort, for addresses that are valid but not well understood.
   */
  bool Describe(Commands::Context &context, Offset address,
                bool explain) const {
    if (_virtualAddressMap == 0) {
      return false;
    }
    typename VirtualAddressMap<Offset>::const_iterator it =
        _virtualAddressMap->find(address);
    if (it == _virtualAddressMap->end()) {
      return false;
    }
    int flags = it.Flags();

    Commands::Output &output = context.GetOutput();
    output << "Address 0x" << std::hex << address;
    if (flags & Attributes::HAS_KNOWN_PERMISSIONS) {
      if (flags & Attributes::IS_READABLE) {
        if (flags & Attributes::IS_WRITABLE) {
          if (flags & Attributes::IS_EXECUTABLE) {
            // This is mildly unexpected.
            output << " is readable, writable and executable.\n";
          } else {
            output << " is readable and writable.\n";
          }
        } else {
          if (flags & Attributes::IS_EXECUTABLE) {
            output << " is readable and executable.\n";
          } else {
            output << " is readable but not writable or executable.\n";
          }
        }
      } else {
        if (flags & Attributes::IS_WRITABLE) {
          if (flags & Attributes::IS_EXECUTABLE) {
            // This is unexpected.
            output << " is writable and executable but not readable!\n";
          } else {
            // This is unexpected.
            output << " is writable but not readable!\n";
          }
        } else {
          if (flags & Attributes::IS_EXECUTABLE) {
            // This is unexpected.
            output << " is executable but not readable!\n";
          } else {
            /*
             * This happens if the process has reserved a virtual address
             * range but is not currently using it.
             */
            output << " is not readable, writable or executable.\n";
          }
        }
      }
    } else {
      output << " has unknown permissions.";
    }
    if ((flags & Attributes::IS_MAPPED) != 0) {
      if ((flags & Attributes::IS_TRUNCATED) != 0) {
        output << "This address is mapped into a truncated section of "
                  "the process image.\n";
      } else {
        output << "This address is mapped into the process image.\n";
      }
    } else {
      output << "This address is not mapped into the process image.\n";
    }
    if (explain) {
      Offset base = it.Base();
      Offset limit = it.Limit();
      output << "This address is at offset 0x" << std::hex << (address - base)
             << " in region [0x" << base << ", 0x" << limit << ").\n";
      if (flags & Attributes::IS_MAPPED) {
        if (flags & Attributes::IS_TRUNCATED) {
          output << "The region is truncated in the process image.\n";
        } else {
          output << "The region is fully mapped in the process image.\n";
        }
      } else {
        output << "The region is not mapped in the process image.\n";
      }
    }
    return true;
  }

 protected:
  const VirtualAddressMap<Offset> *_virtualAddressMap;
};
}  // namespace chap
