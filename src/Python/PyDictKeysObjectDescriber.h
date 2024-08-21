// Copyright (c) 2018-2020 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../Allocations/Graph.h"
#include "../Allocations/PatternDescriber.h"
#include "../ProcessImage.h"

namespace chap {
namespace Python {
template <typename Offset>
class PyDictKeysObjectDescriber : public Allocations::PatternDescriber<Offset> {
 public:
  typedef
      typename Allocations::Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternDescriber<Offset> Base;
  typedef typename Allocations::Directory<Offset>::Allocation Allocation;
  PyDictKeysObjectDescriber(const ProcessImage<Offset>& processImage)
      : Allocations::PatternDescriber<Offset>(processImage, "PyDictKeysObject"),
        _graph(*(processImage.GetAllocationGraph())),
        _directory(_graph.GetAllocationDirectory()),
        _infrastructureFinder(processImage.GetPythonInfrastructureFinder()),
        _strType(_infrastructureFinder.StrType()),
        _cstringInStr(_infrastructureFinder.CstringInStr()),
        _garbageCollectionHeaderSize(
            _infrastructureFinder.GarbageCollectionHeaderSize()),
        _keysInDict(_infrastructureFinder.KeysInDict()),
        _dictKeysHeaderSize(_infrastructureFinder.DictKeysHeaderSize()),
        _contiguousImage(processImage.GetVirtualAddressMap(),
                         processImage.GetAllocationDirectory()) {}

  /*
   * Describe the specified allocation, which has already been pre-tagged
   * as matching the pattern.
   */
  virtual void Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool explain) const {
    Commands::Output& output = context.GetOutput();
    output << "This allocation matches pattern PyDictKeysObject.\n";
    _contiguousImage.SetIndex(index);

    Offset keysAddress = allocation.Address();
    Offset keysLimit = keysAddress + _contiguousImage.Size();
    /*
     * The most common case is that the python arenas are all
     * allocated by mmap() but it is possible, based on an #ifdef
     * that the arenas can be allocated via malloc().  This checks
     * for the latter case in a way that favors the former; if no
     * arenas are malloced, there will not be any outgoing references
     * from the array of arena structures.
     */
    const char* firstTriple = nullptr;
    const char* pastTriples = nullptr;

    Offset triples = 0;
    Offset triplesLimit = 0;
    if (_dictKeysHeaderSize > 0) {
      _infrastructureFinder.GetTriplesAndLimitFromDictKeys(keysAddress, triples,
                                                           triplesLimit);
      if (triples > 0) {
        firstTriple = _contiguousImage.FirstChar() + triples - keysAddress;
        pastTriples = firstTriple + (triplesLimit - triples);
      }
    } else {
      // For older python, we need to obtain the capacity from the dict.

      const AllocationIndex* pFirstIncoming;
      const AllocationIndex* pPastIncoming;
      _graph.GetIncoming(index, &pFirstIncoming, &pPastIncoming);
      Offset minDictSizeWithGCH =
          _garbageCollectionHeaderSize + _keysInDict + sizeof(Offset);
      for (const AllocationIndex* pNextIncoming = pFirstIncoming;
           pNextIncoming != pPastIncoming; pNextIncoming++) {
        AllocationIndex incomingIndex = *pNextIncoming;
        const Allocation* incomingAllocation =
            _directory.AllocationAt(incomingIndex);
        Offset incomingAddress = incomingAllocation->Address();
        Offset incomingSize = incomingAllocation->Size();
        if (incomingSize < minDictSizeWithGCH) {
          continue;
        }
        Offset afterGCH = incomingAddress + _garbageCollectionHeaderSize;
        _infrastructureFinder.GetTriplesAndLimitFromDict(afterGCH, triples,
                                                         triplesLimit);
        if (triples >= keysAddress && triplesLimit <= keysLimit) {
          firstTriple = _contiguousImage.FirstChar() + triples - keysAddress;
          pastTriples = firstTriple + (triplesLimit - triples);
          break;
        }
      }
    }

    if (firstTriple == nullptr) {
      std::cerr << "Warning: Cannot find triples for dictionary keys at 0x"
                << std::hex << keysAddress << ".\n";
      return;
    }

    for (const char* triple = firstTriple; triple < pastTriples;
         triple += 3 * sizeof(Offset)) {
      Offset key = ((Offset*)(triple))[1];
      Offset value = ((Offset*)(triple))[2];
      if (key == 0 || value == 0) {
        continue;
      }
      const char* keyImage;
      Offset numKeyBytesFound =
          Base::_addressMap.FindMappedMemoryImage(key, &keyImage);

      if (numKeyBytesFound < 7 * sizeof(Offset)) {
        continue;
      }
      Offset keyType = ((Offset*)(keyImage))[1];
      Offset keyLength = ((Offset*)(keyImage))[2];

      const char* valueImage;
      Offset numValueBytesFound =
          Base::_addressMap.FindMappedMemoryImage(value, &valueImage);

      if (numValueBytesFound < 7 * sizeof(Offset)) {
        continue;
      }
      Offset valueType = ((Offset*)(valueImage))[1];
      Offset valueLength = ((Offset*)(valueImage))[2];

      if (keyType != valueType) {
        /*
         * For the purposes of this current way of describing, we are
         * only interested in the key/value pairs where both the keys and
         * values are strings.  At some point when the "annotate" command
         * is implemented, this code may be dropped because the user will
         * be able to use that command to find the needed pairs.
         */
        continue;
      }

      if (keyType != _strType || valueType != _strType) {
        continue;
      }
      output << "\"";
      output.ShowEscapedAscii(keyImage + _cstringInStr, keyLength);
      output << "\" : \"";
      output.ShowEscapedAscii(valueImage + _cstringInStr, valueLength);
      output << "\"\n";
    }
    if (explain) {
    }
  }

 private:
  const Allocations::Graph<Offset>& _graph;
  const Allocations::Directory<Offset>& _directory;
  const InfrastructureFinder<Offset>& _infrastructureFinder;
  const Offset _strType;
  const Offset _cstringInStr;
  const Offset _garbageCollectionHeaderSize;
  const Offset _keysInDict;
  const Offset _dictKeysHeaderSize;
  mutable Allocations::ContiguousImage<Offset> _contiguousImage;
};
}  // namespace Python
}  // namespace chap
