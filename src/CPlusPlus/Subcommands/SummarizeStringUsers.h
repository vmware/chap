// Copyright (c) 2023 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../../Allocations/ContiguousImage.h"
#include "../../Allocations/TagHolder.h"
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../../ProcessImage.h"
#include "../../SizedTally.h"
namespace chap {
namespace CPlusPlus {
namespace Subcommands {
template <class Offset>
class SummarizeStringUsers : public Commands::Subcommand {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  typedef typename Allocations::TagHolder<Offset> TagHolder;
  typedef typename TagHolder::TagIndex TagIndex;
  typedef typename TagHolder::TagIndices TagIndices;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename Allocations::Graph<Offset> Graph;
  typedef typename Allocations::EdgePredicate<Offset> EdgePredicate;
  typedef typename Allocations::SignatureDirectory<Offset> SignatureDirectory;
  typedef typename Graph::EdgeIndex EdgeIndex;
  // typedef typename VirtualAddressMap<Offset>::Reader Reader;
  SummarizeStringUsers(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("summarize", "stringusers"),
        _processImage(processImage),
        _directory(processImage.GetAllocationDirectory()),
        _signatureDirectory(processImage.GetSignatureDirectory()),
        _virtualAddressMap(processImage.GetVirtualAddressMap()),
        _contiguousImage(_virtualAddressMap, _directory) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This subcommand summarizes usage of std::string by allocations.\n";
  }

  struct StringStatsForOffset {
    StringStatsForOffset()
        : _numSSOStrings(0),
          _numLongStringAllocations(0),
          _numCOWStrings(0),
          _numEmptyShortSSOStrings(0),
          _numEmptyLongSSOStrings(0),
          _longStringSizeTotal(0),
          _longStringCapacityTotal(0),
          _longStringAllocationSizeTotal(0) {}
    Offset _numSSOStrings;
    Offset _numLongStringAllocations;
    Offset _numCOWStrings;
    Offset _numEmptyShortSSOStrings;
    Offset _numEmptyLongSSOStrings;
    Offset _longStringSizeTotal;
    Offset _longStringCapacityTotal;
    Offset _longStringAllocationSizeTotal;
  };
  std::map<Offset, std::map<Offset, StringStatsForOffset> >
      stringStatsForSignature;

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    Commands::Error& error = context.GetError();
    const Graph* graph = _processImage.GetAllocationGraph();
    if (graph == nullptr) {
      error << "No graph was calculated.\n";
      return;
    }
    const EdgePredicate* edgeIsTainted = _processImage.GetEdgeIsTainted();
    if (edgeIsTainted == nullptr) {
      error << "Edge taints haven't been calculated.\n";
      return;
    }
    const EdgePredicate* edgeIsFavored = _processImage.GetEdgeIsFavored();
    if (edgeIsFavored == nullptr) {
      error << "Favored edges haven't been calculated.\n";
      return;
    }
    const AllocationIndex numAllocations = _directory.NumAllocations();
    const TagHolder& tagHolder = *(_processImage.GetAllocationTagHolder());
    const TagIndices* longStringTagIndices =
        tagHolder.GetTagIndices("%LongString");
    if (longStringTagIndices == nullptr) {
      error << "Pattern LongString is not registered.\n";
      return;
    }
    TagIndices boringSourceTagIndices;
    boringSourceTagIndices.insert(longStringTagIndices->begin(),
                                  longStringTagIndices->end());
    const TagIndices* cowStringBodyTagIndices =
        tagHolder.GetTagIndices("%COWStringBody");
    if (cowStringBodyTagIndices == nullptr) {
      error << "Pattern COWStringBody is not registered.\n";
      return;
    }
    boringSourceTagIndices.insert(cowStringBodyTagIndices->begin(),
                                  cowStringBodyTagIndices->end());
    const TagIndices* dequeMapTagIndices = tagHolder.GetTagIndices("%DequeMap");
    if (dequeMapTagIndices != nullptr) {
      boringSourceTagIndices.insert(dequeMapTagIndices->begin(),
                                    dequeMapTagIndices->end());
    }
    const TagIndices* SSLTagIndices = tagHolder.GetTagIndices("%SSL");
    if (SSLTagIndices != nullptr) {
      boringSourceTagIndices.insert(SSLTagIndices->begin(),
                                    SSLTagIndices->end());
    }
    const TagIndices* SSL_CTXTagIndices = tagHolder.GetTagIndices("%SSL_CTX");
    if (SSL_CTXTagIndices != nullptr) {
      boringSourceTagIndices.insert(SSL_CTXTagIndices->begin(),
                                    SSL_CTXTagIndices->end());
    }

    size_t ssoStringCount = 0;
    size_t emptySsoStringCount = 0;
    size_t longStringCount = 0;
    size_t cowStringReferenceCount = 0;
    ContiguousImage contiguousTargetImage(_virtualAddressMap, _directory);

    for (AllocationIndex i = 0; i < numAllocations; i++) {
      const Allocation* allocation = _directory.AllocationAt(i);
      if (!allocation->IsUsed()) {
        continue;
      }
      if (allocation->Size() < sizeof(Offset)) {
        continue;
      }
      TagIndex referencingTagIndex = tagHolder.GetTagIndex(i);
      if (boringSourceTagIndices.find(referencingTagIndex) !=
          boringSourceTagIndices.end()) {
        // This is just an attempted optimization to avoid starting from
        // allocations that
        // can't hold std::string.
        continue;
      }
      _contiguousImage.SetIndex(i);
      const Offset* offsetLimit = _contiguousImage.OffsetLimit();
      const Offset* firstOffset = _contiguousImage.FirstOffset();
      if (firstOffset != ((Offset*)_contiguousImage.FirstChar())) {
        // This is strangely aligned and not expected to hold
        // std::string.
        continue;
      }
      const Offset address = allocation->Address();

      bool isUnsigned = true;
      Offset signature = *firstOffset;

      if (_signatureDirectory.IsMapped(signature)) {
        isUnsigned = false;
      }

      for (const Offset* check = firstOffset; check < offsetLimit; check++) {
        Offset offsetInReferrer = (check - firstOffset) * sizeof(Offset);
        Offset pointerCandidate = *check;
        if (pointerCandidate == 0) {
          continue;
        }
        if (pointerCandidate & ((sizeof(Offset) - 1) != 0)) {
          continue;
        }
        EdgeIndex edgeIndex = graph->TargetEdgeIndex(i, pointerCandidate);
        AllocationIndex targetIndex = graph->GetTargetForOutgoing(edgeIndex);
        if (targetIndex == numAllocations) {
          // The pointer candidate is not to a different allocation.
          if (pointerCandidate !=
              (address + offsetInReferrer + 2 * sizeof(Offset))) {
            continue;
          }
          if ((check + 4) > offsetLimit) {
            continue;
          }
          Offset length = check[1];
          if (length >= 16) {
            continue;
          }
          if (length == 0) {
            if (*((const char*)(check + 2)) == '\000') {
              ssoStringCount++;
              emptySsoStringCount++;
            }
          } else {
            if (strnlen((const char*)(check + 2), 16) != length) {
              continue;
            }
            ssoStringCount++;
          }
          if (!isUnsigned) {
            StringStatsForOffset& stats =
                stringStatsForSignature[signature][offsetInReferrer];
            stats._numSSOStrings++;
            if (length == 0) {
              stats._numEmptyShortSSOStrings++;
            }
            // TODO: decide if we trust this enough to bump check by 3
          }
          continue;
        }

        /*
         * The pointer is to a different allocation.  Skip if it is tainted.
         */

        if (edgeIsTainted->ForOutgoing(edgeIndex)) {
          continue;
        }

        /*
         * Since both %LongString and %COWStringBody support the notion of
         * favored references, and those are the only 2 target patterns
         * that matter here, skip if the reference is not favored.
         */

        if (!(edgeIsFavored->ForOutgoing(edgeIndex))) {
          continue;
        }

        const Allocation* targetAllocation =
            _directory.AllocationAt(targetIndex);
        if (allocation == nullptr) {
          continue;
        }
        Offset targetAddress = targetAllocation->Address();

        TagIndex targetTagIndex = tagHolder.GetTagIndex(targetIndex);
        if (longStringTagIndices->find(targetTagIndex) !=
            longStringTagIndices->end()) {
          if (targetAddress != pointerCandidate) {
            continue;
          }
          Offset length = check[1];
          Offset capacity = check[2];
          Offset targetAllocationSize = targetAllocation->Size();
          if (capacity >= targetAllocationSize) {
            // The capacity excludes the trailing NULL.
            continue;
          }
          if (length > capacity) {
            continue;
          }
          contiguousTargetImage.SetIndex(targetIndex);
          if (length != strnlen(contiguousTargetImage.FirstChar(),
                                targetAllocationSize)) {
            continue;
          }
          ssoStringCount++;
          longStringCount++;
          if (!isUnsigned) {
            StringStatsForOffset& stats =
                stringStatsForSignature[signature][offsetInReferrer];
            stats._numSSOStrings++;
            stats._numLongStringAllocations++;
            if (length == 0) {
              stats._numEmptyLongSSOStrings++;
            } else {
              stats._longStringSizeTotal += length;
            }
            stats._longStringCapacityTotal += capacity;
            stats._longStringAllocationSizeTotal += targetAllocationSize;
          }
          continue;
        }
        if (cowStringBodyTagIndices->find(targetTagIndex) !=
            cowStringBodyTagIndices->end()) {
          if (targetAddress + 3 * sizeof(Offset) != pointerCandidate) {
            continue;
          }
          cowStringReferenceCount++;
          if (isUnsigned) {
          } else {
            StringStatsForOffset& stats =
                stringStatsForSignature[signature][offsetInReferrer];
            stats._numCOWStrings++;
            // TODO: extract other values from COWStringBody
          }
          continue;
        }
      }
    }
    output << std::dec << ssoStringCount
           << " SSO strings were found in allocations.\n";
    output << std::dec << emptySsoStringCount
           << " empty SSO strings were found in allocations.\n";
    output << std::dec << longStringCount
           << " SSL strings in allocations used %LongString.\n";
    if (cowStringReferenceCount > 0) {
      output << "The program appears to be using COW strings from a pre-C++11 "
                "ABI.\n";
      if (ssoStringCount > 0) {
        output << "The program also appears to be SSO strings from the C++11 "
                  "ABI.\n";
        output << "This may indicate a conflict between how modules are "
                  "compiled.\n";
      }
      // To support CSB, we really need to figure out the pointer to
      // the canonical empty string, and go back and find them.
    }
    for (auto const& signatureAndMap : stringStatsForSignature) {
      Offset signature = signatureAndMap.first;
      output << "String usage for signature 0x" << std::hex << signature;
      std::string signatureName = _signatureDirectory.Name(signature);
      if (!signatureName.empty()) {
        output << " (" << signatureName << ")";
      }
      output << "\n";
      Offset maxSSOStrings = 0;
      for (auto const& offsetAndStats : signatureAndMap.second) {
        const auto& stats = offsetAndStats.second;
        Offset numSSOStrings = stats._numSSOStrings;
        if (maxSSOStrings < numSSOStrings) {
          maxSSOStrings = numSSOStrings;
        }
      }
      for (auto const& offsetAndStats : signatureAndMap.second) {
        Offset offset = offsetAndStats.first;
        const auto& stats = offsetAndStats.second;
        Offset numSSOStrings = stats._numSSOStrings;
        if (numSSOStrings != maxSSOStrings) {
          // Some of the patterns, especially in the case that the long
          // buffer is not used and the string is empty, are rather weak.
          continue;
        }
        if (stats._numLongStringAllocations > 0) {
          output << "   " << std::dec << numSSOStrings
                 << " SSO strings at offset 0x" << std::hex << offset
                 << " take a total of 0x"
                 << stats._longStringAllocationSizeTotal << " bytes in "
                 << std::dec << stats._numLongStringAllocations
                 << " %LongString allocations.\n";
          if (stats._longStringSizeTotal * 2 < stats._longStringCapacityTotal) {
            output
                << "   ... Possibly reducing the capacity would help here.\n";
          }
        }
        Offset numEmptyShortSSOStrings = stats._numEmptyShortSSOStrings;
        if (numEmptyShortSSOStrings * 2 > numSSOStrings) {
          /*
           * If more than half of the short strings are empty, there is an
           * opportunity to save memory, at the cost of CPU time, by changing
           * the string field to be a pointer to a dynamically allocated string
           * with the pointer being NULL if the string is empty.
           */
          Offset percentEmptyShortStrings =
              (numEmptyShortSSOStrings * 1000 / numSSOStrings + 5) / 10;
          output << "   " << std::dec << percentEmptyShortStrings << "% of the "
                 << numSSOStrings << " SSO strings at offset 0x" << std::hex
                 << offset << " are empty (without an extra buffer).\n";
        }
      }
    }
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const Allocations::Directory<Offset>& _directory;
  const SignatureDirectory& _signatureDirectory;
  const VirtualAddressMap<Offset>& _virtualAddressMap;
  ContiguousImage _contiguousImage;
};
}  // namespace Subcommands
}  // namespace CPlusPlus
}  // namespace chap
