// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../../SizedTally.h"
#include "../Finder.h"
#include "../SignatureSummary.h"
#include "../TagHolder.h"
namespace chap {
namespace Allocations {
namespace Visitors {
template <class Offset>
class Summarizer {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  typedef typename SignatureSummary<Offset>::Item SummaryItem;
  class Factory {
   public:
    Factory() : _commandName("summarize") {}
    Summarizer* MakeVisitor(Commands::Context& context,
                            const ProcessImage<Offset>& processImage) {
      bool sortByCount = true;
      size_t numSortBy = context.GetNumArguments("sortby");
      if (numSortBy > 0) {
        if (numSortBy > 1) {
          context.GetError() << "At most one /sortby switch is allowed.\n";
          return (Summarizer*)(0);
        }
        const std::string sortBy = context.Argument("sortby", 0);
        if (sortBy == "bytes") {
          sortByCount = false;
        } else {
          if (sortBy != "count") {
            context.GetError() << "Unknown /sortby argument \"" << sortBy
                               << "\"\n";
            return (Summarizer*)(0);
          }
        }
      }
      return new Summarizer(context, processImage.GetSignatureDirectory(),
                            *(processImage.GetAllocationTagHolder()),
                            processImage.GetVirtualAddressMap(), sortByCount);
    }
    const std::string& GetCommandName() const { return _commandName; }
    // TODO: allow adding taints
    const std::vector<std::string>& GetTaints() const { return _taints; }
    void ShowHelpMessage(Commands::Context& context) {
      Commands::Output& output = context.GetOutput();
      output << "In this case \"summarize\" means show the tally and byte"
                " count associated with\neach type (as determined by the"
                " signature, if any) or pattern and with a\n"
                "separate tally and byte count for unsigned allocations.\n";
      output << "Use \"/sortby bytes\" to sort summary by total bytes "
                "rather than allocation count\n";
    }

   private:
    const std::string _commandName;
    const std::vector<std::string> _taints;
  };

  Summarizer(Commands::Context& context,
             const SignatureDirectory<Offset>& signatureDirectory,
             const TagHolder<Offset>& tagHolder,
             const VirtualAddressMap<Offset>& addressMap, bool sortByCount)
      : _context(context),
        _signatureSummary(signatureDirectory, tagHolder),
        _addressMap(addressMap),
        _sizedTally(context, "allocations"),
        _sortByCount(sortByCount) {}
  ~Summarizer() {
    std::vector<SummaryItem> items;
    if (_sortByCount) {
      _signatureSummary.SummarizeByCount(items);
    } else {
      _signatureSummary.SummarizeByBytes(items);
    }
    DumpSummaryItems(items);
  }
  void Visit(AllocationIndex index, const Allocation& allocation) {
    size_t size = allocation.Size();
    const char* image;
    Offset numBytesFound =
        _addressMap.FindMappedMemoryImage(allocation.Address(), &image);
    if (numBytesFound < size) {
      // This is not expected to happen on Linux.
      size = numBytesFound;
    }
    _sizedTally.AdjustTally(size);
    _signatureSummary.AdjustTally(index, size, image);
  }

 private:
  Commands::Context& _context;
  SignatureSummary<Offset> _signatureSummary;
  const VirtualAddressMap<Offset>& _addressMap;
  SizedTally<Offset> _sizedTally;
  bool _sortByCount;
  static std::string InDecimalWithCommas(Offset n) {  // treat as positive
    if (n == 0) {
      return "0";
    } else {
      char chars[22];
      char* p = chars + 22;
      *--p = (char)0;
      int numDigits = 0;
      while (n != 0) {
        if (numDigits > 0 && (numDigits % 3) == 0) {
          *--p = ',';
        }
        numDigits++;
        *--p = (char)(0x30 + n % 10);
        n = n / 10;
      }
      return p;
    }
  }

  void DumpSummaryItems(const std::vector<SummaryItem>& items) {
    Commands::Output& output = _context.GetOutput();
    for (typename std::vector<SummaryItem>::const_iterator it = items.begin();
         it != items.end(); ++it) {
      if (it->_name.empty()) {
        Offset signature = it->_subtotals.begin()->first;
        output << "Signature " << std::hex << signature << " has " << std::dec
               << it->_totals._count << " instances taking 0x" << std::hex
               << it->_totals._bytes << "("
               << InDecimalWithCommas(it->_totals._bytes) << ")"
               << " bytes.\n";
      } else {
        if (it->_name[0] == '%') {
          output << "Pattern " << it->_name << " has " << std::dec
                 << it->_totals._count << " instances taking 0x" << std::hex
                 << it->_totals._bytes << "("
                 << InDecimalWithCommas(it->_totals._bytes) << ")"
                 << " bytes.\n";
          for (typename std::vector<std::pair<
                   Offset, typename SignatureSummary<Offset>::Tally> >::
                   const_iterator itSub = it->_subtotals.begin();
               itSub != it->_subtotals.end(); ++itSub) {
            output << "   Matches of size 0x" << std::hex << itSub->first
                   << " have " << std::dec << itSub->second._count
                   << " instances taking 0x" << std::hex << itSub->second._bytes
                   << "(" << InDecimalWithCommas(itSub->second._bytes) << ")"
                   << " bytes.\n";
          }
        } else if (it->_name == "?") {
          // Unrecognized allocations.
          output << "Unrecognized allocations have " << std::dec
                 << it->_totals._count << " instances taking 0x" << std::hex
                 << it->_totals._bytes << "("
                 << InDecimalWithCommas(it->_totals._bytes) << ")"
                 << " bytes.\n";
          for (typename std::vector<std::pair<
                   Offset, typename SignatureSummary<Offset>::Tally> >::
                   const_iterator itSub = it->_subtotals.begin();
               itSub != it->_subtotals.end(); ++itSub) {
            output << "   Unrecognized allocations of size 0x" << std::hex
                   << itSub->first << " have " << std::dec
                   << itSub->second._count << " instances taking 0x" << std::hex
                   << itSub->second._bytes << "("
                   << InDecimalWithCommas(itSub->second._bytes) << ")"
                   << " bytes.\n";
          }
        } else if (it->_subtotals.size() == 1) {
          // Just one summarized signature matched the given name.
          output << "Signature " << std::hex << it->_subtotals.begin()->first
                 << " (" << it->_name << ") has " << std::dec
                 << it->_totals._count << " instances taking 0x" << std::hex
                 << it->_totals._bytes << "("
                 << InDecimalWithCommas(it->_totals._bytes) << ")"
                 << " bytes.\n";
        } else {
          // Multiple summarized signatures matched the name.
          output << "Multiple signatures for " << it->_name
                 << " have a total of " << std::dec << it->_totals._count
                 << " instances taking 0x" << std::hex << it->_totals._bytes
                 << "(" << InDecimalWithCommas(it->_totals._bytes) << ")"
                 << " bytes:\n";
          for (typename std::vector<std::pair<
                   Offset, typename SignatureSummary<Offset>::Tally> >::
                   const_iterator itSub = it->_subtotals.begin();
               itSub != it->_subtotals.end(); ++itSub) {
            output << "   Signature " << std::hex << itSub->first << " has "
                   << std::dec << itSub->second._count << " instances taking 0x"
                   << std::hex << itSub->second._bytes << "("
                   << InDecimalWithCommas(itSub->second._bytes) << ")"
                   << " bytes.\n";
          }
        }
      }
    }
  }
};
}  // namespace Visitors
}  // namespace Allocations
}  // namespace chap
