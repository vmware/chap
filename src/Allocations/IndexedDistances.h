// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
namespace chap {
namespace Allocations {
template <typename Index>
class IndexedDistances {
 public:
  IndexedDistances(Index numIndices)
      : _numIndices(numIndices),
        _distanceBits(8),
        _maxDistanceBits(32),
        _maxDistance(0xFF) {
    _distances8.reserve(_numIndices);
    _distances8.resize(_numIndices, 0);
  }

  void SetDistance(Index index, Index distance) {
    while (distance > _maxDistance) {
      if (_distanceBits == _maxDistanceBits) {
        abort();
      }
      if (_distanceBits == 8) {
        _distances16.reserve(_numIndices);
        _distances16.resize(_numIndices, 0);
        for (size_t i = 0; i < _numIndices; ++i) {
          _distances16[i] = _distances8[i];
        }
        _distanceBits = 16;
        _maxDistance = 0xFFFF;
        std::vector<uint8_t> distances8;
        distances8.swap(_distances8);
      } else if (_distanceBits == 16) {
        _distances32.reserve(_numIndices);
        _distances32.resize(_numIndices, 0);
        for (size_t i = 0; i < _numIndices; ++i) {
          _distances32[i] = _distances16[i];
        }
        _distanceBits = 32;
        _maxDistance = 0xFFFFFFFF;
        std::vector<uint16_t> distances16;
        distances16.swap(_distances16);
      } else {
        abort();
      }
    }
    if (_distanceBits == 8) {
      _distances8[index] = distance & 0xFF;
    } else if (_distanceBits == 16) {
      _distances16[index] = distance & 0xFFFF;
    } else {
      _distances32[index] = distance & 0xFFFFFFFF;
    }
  }

  Index GetDistance(Index index) const {
    if (_distanceBits == 8) {
      return _distances8[index];
    } else if (_distanceBits == 16) {
      return _distances16[index];
    } else {
      return _distances32[index];
    }
  }

 private:
  Index _numIndices;
  uint16_t _distanceBits;
  uint16_t _maxDistanceBits;
  Index _maxDistance;
  std::vector<uint8_t> _distances8;
  std::vector<uint16_t> _distances16;
  std::vector<uint32_t> _distances32;
};
}  // namespace Allocations
}  // namespace chap
