// Copyright (c) 2024-2025 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string_view>

#include "../Allocations/ContiguousImage.h"
#include "../Allocations/Directory.h"
#include "../Annotator.h"
#include "../Commands/Runner.h"

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class SSOStringAnnotator : public Annotator<Offset> {
 public:
  typedef typename Allocations::Directory<Offset> Directory;
  typedef typename Allocations::ContiguousImage<Offset> ContiguousImage;
  typedef typename Directory::AllocationIndex AllocationIndex;
  typedef typename Directory::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;
  SSOStringAnnotator(const ProcessImage<Offset>& processImage)
      : Annotator<Offset>("SSOString"),
        _processImage(processImage),
        _addressMap(processImage.GetVirtualAddressMap()),
        _directory(processImage.GetAllocationDirectory()),
        _numAllocations(_directory.NumAllocations()),
        _tagHolder(*(processImage.GetAllocationTagHolder())),
        _tagIndices(_tagHolder.GetTagIndices("%LongString")) {}

  /*
   * Provide the actual annotation lines, excluding the information about the
   * address and the limit, with the specified prefix at the start of each line.
   */
  virtual Offset Annotate(
      Commands::Context& context,
      typename VirtualAddressMap<Offset>::Reader& reader,
      typename Annotator<Offset>::WriteHeaderFunction writeHeader,
      Offset address, Offset limit, const std::string& prefix) const {
    if (address + 4 * sizeof(Offset) > limit) {
      return address;
    }
    Offset buffer = reader.ReadOffset(address, ~0);
    if ((buffer & (sizeof(Offset) - 1)) != 0) {
      return address;
    }
    Offset length = reader.ReadOffset(address + sizeof(Offset), ~0);
    Offset contents[2];
    const char* chars = (char*)(contents);
    AllocationIndex index = _numAllocations;
    if (buffer == address + 2 * sizeof(Offset)) {
      if (length > 15) {
        return address;
      }
      contents[0] = reader.ReadOffset(address + 2 * sizeof(Offset), 0);
      contents[1] = reader.ReadOffset(address + 3 * sizeof(Offset), 0);
    } else {
      Offset capacity = reader.ReadOffset(address + 2 * sizeof(Offset), 0);
      if (capacity < length) {
        return address;
      }
      if (capacity < 2 * sizeof(Offset)) {
        return address;
      }
      AllocationIndex index = _directory.AllocationIndexOf(buffer);
      if (index == _numAllocations) {
        return address;
      }

      const Allocation* allocation = _directory.AllocationAt(index);
      if (allocation == nullptr) {
        return address;
      }
      if (allocation->Address() != buffer) {
        return address;
      }
      if (capacity < _directory.MinRequestSize(index)) {
        return address;
      }
      if (capacity >= allocation->Size()) {
        return address;
      }
      Allocations::ContiguousImage<Offset> charsImage(_addressMap, _directory);

      charsImage.SetIndex(index);
      chars = charsImage.FirstChar();
    }

    if (chars[length] != 0) {
      return address;
    }
    if (length > 0 && (chars[length - 1] == 0)) {
      return address;
    }
    if (strlen(chars) != length) {
      return address;
    }

    if (index != _numAllocations) {
      // This check is deferred to this point because it is relatively
      // expensive.
      if ((_tagIndices == nullptr) || (_tagIndices->find(_tagHolder.GetTagIndex(
                                           index)) == _tagIndices->end())) {
        return address;
      }
    }

    /*
     * Put the header for the annotation, showing the range covered and
     * the annotator name.
     */
    writeHeader(address, address + 4 * sizeof(Offset),
                Annotator<Offset>::_name);

    /*
     * The caller will put the header line, with the relevant addresses
     * and the annotator name.
     */
    Commands::Output& output = context.GetOutput();
    output << prefix << "SSO string with length 0x" << std::hex << length
           << " and  contents";
    size_t charsAvailable = 80 - prefix.size() - 2;
    if (length <= charsAvailable) {
      output << "\n" << prefix << "\"" << chars << "\"\n";
    } else {
      output << " starting with\n"
             << prefix << "\"" << std::string_view(chars, charsAvailable)
             << "\"\n";
    }
    return address + 4 * sizeof(Offset);
  }

 private:
  const ProcessImage<Offset>& _processImage;
  const VirtualAddressMap<Offset>& _addressMap;
  const Directory& _directory;
  AllocationIndex _numAllocations;
  const Allocations::TagHolder<Offset>& _tagHolder;
  const typename Allocations::TagHolder<Offset>::TagIndices* _tagIndices;
};
}  // namespace CPlusPlus
}  // namespace chap
