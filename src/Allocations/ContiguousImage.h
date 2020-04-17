// Copyright (c) 2019-2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "../VirtualAddressMap.h"
#include "Directory.h"
namespace chap {
namespace Allocations {
template <class Offset>
class ContiguousImage {
 public:
  typedef typename Directory<Offset>::AllocationIndex Index;
  typedef typename Directory<Offset>::Allocation Allocation;

  ContiguousImage(const VirtualAddressMap<Offset> &addressMap,
                  const Directory<Offset> &directory)
      : _directory(directory),
        _numAllocations(directory.NumAllocations()),
        _index(_numAllocations),
        _maxAllocationSize(directory.MaxAllocationSize()),
        _buffer((_maxAllocationSize / sizeof(Offset)) + 2, 0),
        _bufferAsChars((char *)(&(_buffer[0]))),
        _bufferAsOffsets((Offset *)(&(_buffer[0]))),
        _pFirstChar(_bufferAsChars),
        _size(0),
        _pFirstOffset(_bufferAsOffsets),
        _pPastOffsets(_bufferAsOffsets),
        _addressMap(addressMap),
        _iterator(_addressMap.end()),
        _endIterator(_addressMap.end()),
        _regionImage(nullptr),
        _regionBase(0),
        _regionLimit(0) {}

  void SetIndex(Index index) {
    if (index > _numAllocations) {
      index = _numAllocations;
    }
    if (index != _index) {
      _index = index;
      _pFirstChar = _bufferAsChars;
      _pFirstOffset = _bufferAsOffsets;
      _pPastOffsets = _bufferAsOffsets;
      const Allocation *allocation = _directory.AllocationAt(index);
      if (allocation != nullptr) {
        Offset address = allocation->Address();
        Offset size = allocation->Size();
        Offset limit = address + size;
        if (_regionBase > address || address >= _regionLimit) {
          _regionImage = nullptr;
          _regionBase = 0;
          _regionLimit = 0;
          _iterator = _addressMap.find(address);
          if (_iterator != _endIterator) {
            _regionImage = _iterator.GetImage();
          }
          if (_regionImage != nullptr) {
            _regionBase = _iterator.Base();
            _regionLimit = _iterator.Limit();
          } else {
            return;
          }
        }
        if (limit <= _regionLimit) {
          _pFirstChar = _regionImage + (address - _regionBase);
          _size = size;
        } else {
          /*
           * This is very rare on Linux but could happen in the case of
           * truncation.  It does happen even without truncation on Windows,
           * which may be supported at some point.
           */
          memcpy(_bufferAsChars, _regionImage + (address - _regionBase),
                 _regionLimit - address);
          Offset copiedTo = _regionLimit;
          while (copiedTo < limit) {
            _regionBase = 0;
            _regionLimit = 0;
            _regionImage = nullptr;
            if (++_iterator == _endIterator) {
              break;
            }
            _regionImage = _iterator.GetImage();
            if (_regionImage == nullptr) {
              continue;
            }
            _regionBase = _iterator.Base();
            _regionLimit = _iterator.Limit();
            if (_regionBase > limit) {
              break;
            }
            if (_regionBase > copiedTo) {
              memset(_bufferAsChars + (copiedTo - address), 0,
                     _regionBase - copiedTo);
              copiedTo = _regionBase;
            }
            if (_regionLimit >= limit) {
              memcpy(_bufferAsChars + (copiedTo - address),
                     _regionImage + (copiedTo - _regionBase), limit - copiedTo);
              copiedTo = limit;
            } else {
              memcpy(_bufferAsChars + (copiedTo - address),
                     _regionImage + (copiedTo - _regionBase),
                     _regionLimit - copiedTo);
              copiedTo = _regionLimit;
            }
          }
          if (copiedTo < limit) {
            memset(_bufferAsChars + (copiedTo - address), 0, limit - copiedTo);
          }
          _size = size;
        }
        if (_size != 0 && (address & (sizeof(Offset) - 1)) == 0) {
          _pFirstOffset = (Offset *)(_pFirstChar);
          _pPastOffsets =
              (Offset *)(_pFirstChar + (size & ~(sizeof(Offset) - 1)));
        }
      }
    }
  }
  Index GetIndex() const { return _index; }
  const Offset *FirstOffset() const { return _pFirstOffset; }
  const Offset *OffsetLimit() const { return _pPastOffsets; }
  const char *FirstChar() const { return _pFirstChar; }
  Offset Size() const { return _size; }

 private:
  const Directory<Offset> &_directory;
  const Index _numAllocations;
  Index _index;
  const Offset _maxAllocationSize;
  std::vector<Offset> _buffer;
  char *_bufferAsChars;
  Offset *_bufferAsOffsets;
  const char *_pFirstChar;
  Offset _size;
  const Offset *_pFirstOffset;
  const Offset *_pPastOffsets;
  const VirtualAddressMap<Offset> &_addressMap;
  typename VirtualAddressMap<Offset>::const_iterator _iterator;
  typename VirtualAddressMap<Offset>::const_iterator _endIterator;
  const char *_regionImage;
  Offset _regionBase;
  Offset _regionLimit;
};
}  // namespace Allocations
}  // namespace chap
