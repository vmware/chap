// Copyright (c) 2017,2019-2020,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <algorithm>
#include <map>
#include <set>
#include "Directory.h"
#include "SignatureDirectory.h"
#include "TagHolder.h"
namespace chap {
namespace Allocations {
template <class Offset>
class SignatureSummary {
 public:
  struct Tally {
    Tally() : _count(0), _bytes(0) {}
    Tally(Offset count, Offset bytes) : _count(count), _bytes(bytes) {}
    Tally& operator=(const Tally& other) = default;
    void Bump(Offset size) {
      _count++;
      _bytes += size;
    }
    Offset _count;
    Offset _bytes;
  };
  typedef std::map<Offset, Offset> SizeToCount;
  struct TallyWithSizeSubtotals {
    Tally _tally;
    SizeToCount _sizeToCount;
    void Bump(Offset size) {
      _tally.Bump(size);
      ++(_sizeToCount.try_emplace(size, 0).first->second);
    }
  };
  typedef std::map<Offset, Tally> OffsetToTally;
  typedef typename OffsetToTally::iterator OffsetToTallyIterator;
  typedef typename OffsetToTally::const_iterator OffsetToTallyConstIterator;
  typedef std::map<std::string, Tally> NameToTally;
  typedef typename NameToTally::iterator NameToTallyIterator;
  typedef typename NameToTally::const_iterator NameToTallyConstIterator;
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  struct Item {
    std::string _name;
    Tally _totals;
    std::vector<std::pair<Offset, Tally> > _subtotals;
    void AddSubtotal(Offset signature, const Tally& tally) {
      _subtotals.emplace_back(signature, tally);
    }
  };

  SignatureSummary(const SignatureDirectory<Offset>& directory,
                   const TagHolder<Offset>& tagHolder)
      : _directory(directory), _tagHolder(tagHolder) {}

  bool AdjustTally(AllocationIndex index, Offset size, const char* image) {
    const std::string& tagName = _tagHolder.GetTagName(index);
    if (!tagName.empty()) {
      /*
       * Tags take precedent over any signature.
       */
      _talliesWithSizeSubtotals[tagName].Bump(size);
    } else {
      Offset signature = 0;
      if (size >= sizeof(Offset)) {
        signature = *((Offset*)image);
      }
      if (_directory.IsMapped(signature)) {
        TallyBySignature(signature, size);
        std::string name = _directory.Name(signature);
        if (!name.empty()) {
          TallyByName(name, size);
        }
      } else {
        _unsignedTallyWithSizeSubtotals.Bump(size);
      }
    }
    return false;
  }

  void SummarizeByCount(std::vector<Item>& items) const {
    FillItems(items);
    for (auto& item : items) {
      if (item._subtotals.size() > 1) {
        std::sort(item._subtotals.begin(), item._subtotals.end(),
                  CompareSubtotalsByCount());
      }
    }
    std::sort(items.begin(), items.end(), CompareItemsByCount());
  }

  void SummarizeByBytes(std::vector<Item>& items) const {
    FillItems(items);
    for (auto& item : items) {
      if (item._subtotals.size() > 1) {
        std::sort(item._subtotals.begin(), item._subtotals.end(),
                  CompareSubtotalsByBytes());
      }
    }
    std::sort(items.begin(), items.end(), CompareItemsByBytes());
  }

 private:
  const SignatureDirectory<Offset>& _directory;
  const TagHolder<Offset>& _tagHolder;
  OffsetToTally _signatureToTally;
  NameToTally _nameToTally;
  TallyWithSizeSubtotals _unsignedTallyWithSizeSubtotals;
  std::unordered_map<std::string, TallyWithSizeSubtotals>
      _talliesWithSizeSubtotals;

  void TallyBySignature(Offset signature, Offset size) {
    Tally& tally = _signatureToTally.try_emplace(signature).first->second;
    tally._count++;
    tally._bytes += size;
  }

  void TallyByName(std::string name, Offset size) {
    Tally& tally = _nameToTally.try_emplace(name).first->second;
    tally._count++;
    tally._bytes += size;
  }

  void FillItems(std::vector<Item>& items) const {
    items.clear();
    if (_unsignedTallyWithSizeSubtotals._tally._count > 0) {
      Item& item = items.emplace_back();
      item._name = "?";
      item._totals = _unsignedTallyWithSizeSubtotals._tally;
      for (const auto& sizeAndCount :
           _unsignedTallyWithSizeSubtotals._sizeToCount) {
        item.AddSubtotal(sizeAndCount.first,
                         Tally(sizeAndCount.second,
                               sizeAndCount.first * sizeAndCount.second));
      }
    }
    for (const auto nameAndTally : _talliesWithSizeSubtotals) {
      Item& item = items.emplace_back();
      item._name = nameAndTally.first;
      TallyWithSizeSubtotals tallyWithSizeSubtotals = nameAndTally.second;
      item._totals = tallyWithSizeSubtotals._tally;
      for (const auto& sizeAndCount : tallyWithSizeSubtotals._sizeToCount) {
        item.AddSubtotal(sizeAndCount.first,
                         Tally(sizeAndCount.second,
                               sizeAndCount.first * sizeAndCount.second));
      }
    }
    FillUnnamedSignatures(items);
    FillNamedSignatures(items);
  }

  void FillUnnamedSignatures(std::vector<Item>& items) const {
    for (const auto& signatureAndTally: _signatureToTally) {
      std::string name = _directory.Name(signatureAndTally.first);
      if (name.empty()) {
        Item& item = items.emplace_back();
        item._totals = signatureAndTally.second;
        item.AddSubtotal(signatureAndTally.first, signatureAndTally.second);
      }
    }
  }
  void FillNamedSignatures(std::vector<Item>& items) const {
    for (NameToTallyConstIterator it = _nameToTally.begin();
         it != _nameToTally.end(); ++it) {
      const std::string& name = it->first;
      Item& item = items.emplace_back();
      item._name = name;
      item._totals = it->second;

      for (Offset signature: _directory.Signatures(name)) {
        OffsetToTallyConstIterator itTally = _signatureToTally.find(signature);
        if (itTally != _signatureToTally.end()) {
          item.AddSubtotal(signature, itTally->second);
        }
      }
    }
  }
  struct CompareSubtotalsByCount {
    bool operator()(const std::pair<Offset, Tally>& left,
                    const std::pair<Offset, Tally>& right) {
      return (left.second._count > right.second._count) ||
             ((left.second._count == right.second._count) &&
              (left.first < right.first));
    }
  };

  struct CompareSubtotalsByBytes {
    bool operator()(const std::pair<Offset, Tally>& left,
                    const std::pair<Offset, Tally>& right) {
      return (left.second._bytes > right.second._bytes) ||
             ((left.second._bytes == right.second._bytes) &&
              (left.first < right.first));
    }
  };

  struct CompareItemsByCount {
    bool operator()(const Item& left, const Item& right) {
      return (left._totals._count > right._totals._count) ||
             ((left._totals._count == right._totals._count) &&
              ((left._name < right._name) ||
               ((left._name == right._name) &&
                (left._subtotals.begin()->first <
                 right._subtotals.begin()->first))));
    }
  };

  struct CompareItemsByBytes {
    bool operator()(const Item& left, const Item& right) {
      return (left._totals._bytes > right._totals._bytes) ||
             (left._totals._bytes == right._totals._bytes &&
              ((left._name < right._name) ||
               ((left._name == right._name) &&
                (left._subtotals.begin()->first) <
                    (right._subtotals.begin()->first))));
    }
  };
};
}  // namespace Allocations
}  // namespace chap
