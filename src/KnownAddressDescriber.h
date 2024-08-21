// Copyright (c) 2017-2019 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Describer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>

/*
 * This particular describer is used for the case where the range containing
 * the address is in the process image but chap basically knows nothing about
 * it other than possibly the permissions and possibly a range narrowed by
 * understanding the adjacent ranges.
 */
class KnownAddressDescriber : public Describer<Offset> {
  typedef typename VirtualAddressMap<Offset>::RangeAttributes Attributes;
  typedef typename VirtualMemoryPartition<Offset>::ClaimedRanges ClaimedRanges;

 public:
  KnownAddressDescriber(const ProcessImage<Offset> &processImage)
      : _virtualMemoryPartition(processImage.GetVirtualMemoryPartition()),
        _inaccessibleRanges(
            _virtualMemoryPartition.GetClaimedInaccessibleRanges()),
        _readOnlyRanges(_virtualMemoryPartition.GetClaimedReadOnlyRanges()),
        _rxOnlyRanges(_virtualMemoryPartition.GetClaimedRXOnlyRanges()),
        _writableRanges(_virtualMemoryPartition.GetClaimedWritableRanges()),
        _virtualAddressMap(processImage.GetVirtualAddressMap()) {}

  /*
   * If the address is understood, provide a description for the address,
   * optionally with an additional explanation of why the address matches
   * the description, and return true.  Otherwise don't write anything
   * and return false.  Show addresses only if requested.
   */
  bool Describe(Commands::Context &context, Offset address, bool explain,
                bool showAddresses) const {
    typename VirtualAddressMap<Offset>::const_iterator itMap =
        _virtualAddressMap.find(address);
    if (itMap == _virtualAddressMap.end()) {
      return false;
    }
    int flags = itMap.Flags();
    Offset base = itMap.Base();
    Offset limit = itMap.Limit();

    const ClaimedRanges &ranges = (flags & Attributes::IS_WRITABLE)
                                      ? _writableRanges
                                      : (flags & Attributes::IS_EXECUTABLE)
                                            ? _rxOnlyRanges
                                            : (flags & Attributes::IS_READABLE)
                                                  ? _readOnlyRanges
                                                  : _inaccessibleRanges;
    typename ClaimedRanges::const_iterator itRange = ranges.find(address);
    if (itRange != ranges.end()) {
      base = itRange->_base;
      limit = itRange->_limit;
    }

    Commands::Output &output = context.GetOutput();
    if (showAddresses) {
      output << "Address 0x" << std::hex << address;
      output << " is at offset 0x" << std::hex << (address - base) << " in [0x"
             << base << ", 0x" << limit << "),\nwhich";
    } else {
      output << "This";
    }
    if (flags & Attributes::HAS_KNOWN_PERMISSIONS) {
      if (flags & Attributes::IS_READABLE) {
        if (flags & Attributes::IS_WRITABLE) {
          if (flags & Attributes::IS_EXECUTABLE) {
            // This is mildly unexpected.
            output << " is readable, writable and executable";
          } else {
            output << " is readable and writable";
          }
        } else {
          if (flags & Attributes::IS_EXECUTABLE) {
            output << " is readable and executable";
          } else {
            output << " is readable but not writable or executable";
          }
        }
      } else {
        if (flags & Attributes::IS_WRITABLE) {
          if (flags & Attributes::IS_EXECUTABLE) {
            // This is unexpected.
            output << " is (unexpectedly) writable and executable but not "
                      "readable";
          } else {
            // This is unexpected.
            output << " is (unexpectedly) writable but not readable";
          }
        } else {
          if (flags & Attributes::IS_EXECUTABLE) {
            // This is unexpected.
            output << " is (unexpectedly) executable but not readable";
          } else {
            /*
             * This happens if the process has reserved a virtual address
             * range but is not currently using it.
             */
            output << " is not readable, writable or executable";
          }
        }
      }
    } else {
      output << " has unknown permissions";
    }
    if ((flags & Attributes::IS_MAPPED) != 0) {
      if ((flags & Attributes::IS_TRUNCATED) != 0) {
        output << "\nand is missing due to truncation of "
                  "the process image";
      } else {
        output << "\nand is mapped into the process image";
      }
    } else {
      output << "\nand is not mapped into the process image";
    }
    output << ".\n";
    if (explain) {
      if (showAddresses) {
        output << "Address 0x" << std::hex << address << " is at offset 0x"
               << std::hex << (address - base) << " in region [0x" << base
               << ", 0x" << limit << ").\n";
      }
      if ((flags & Attributes::IS_MAPPED) != 0) {
        if (flags & Attributes::IS_TRUNCATED) {
          output << "The region is missing due to a truncated process image.\n";
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
  const VirtualMemoryPartition<Offset> &_virtualMemoryPartition;
  const ClaimedRanges &_inaccessibleRanges;
  const ClaimedRanges &_readOnlyRanges;
  const ClaimedRanges &_rxOnlyRanges;
  const ClaimedRanges &_writableRanges;
  const VirtualAddressMap<Offset> &_virtualAddressMap;
};
}  // namespace chap
