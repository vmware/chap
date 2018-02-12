// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "FileImage.h"
#include "RangeMapper.h"
namespace chap {
template <typename OffsetType>
class VirtualAddressMap {
 public:
  typedef OffsetType Offset;
  struct RangeAttributes {
    RangeAttributes() : _adjustToFileOffset(0), _flags(0) {}
    RangeAttributes(Offset adjustToFileOffset)
        : _adjustToFileOffset(adjustToFileOffset), _flags(0) {}
    RangeAttributes(Offset adjustToFileOffset, int flags)
        : _adjustToFileOffset(adjustToFileOffset), _flags(flags) {}
    bool operator==(const RangeAttributes &other) {
      return _adjustToFileOffset == other._adjustToFileOffset &&
             _flags == other._flags;
    }
    Offset _adjustToFileOffset;
    static const int IS_READABLE = 0x01;
    static const int IS_WRITABLE = 0x02;
    static const int IS_EXECUTABLE = 0x04;
    static const int HAS_KNOWN_PERMISSIONS = 0x08;
    static const int IS_MAPPED = 0x10;  // mapped, but possibly truncated
    static const int IS_TRUNCATED = 0x20;
    static const int PERMISSIONS_MASK = 0x0f;
    int _flags;
  };
  typedef RangeMapper<Offset, RangeAttributes> RangeFileOffsetMapper;
  template <class OneWayIterator>
  class RangeIterator {
   public:
    RangeIterator(OneWayIterator it, const char *fileImage)
        : _oneWayIterator(it), _fileImage(fileImage) {}
    bool operator==(const RangeIterator &other) {
      return other._oneWayIterator == _oneWayIterator &&
             other._fileImage == _fileImage;
    }
    bool operator!=(const RangeIterator &other) {
      return other._oneWayIterator != _oneWayIterator ||
             other._fileImage != _fileImage;
    }
    RangeIterator &operator++() {
      ++_oneWayIterator;
      return *this;
    }

    const char *GetImage() {
      const typename RangeFileOffsetMapper::Range &range = *_oneWayIterator;
      if ((range._value._flags &
           (RangeAttributes::IS_MAPPED | RangeAttributes::IS_TRUNCATED)) !=
          RangeAttributes::IS_MAPPED) {
        return 0;
      } else {
        return _fileImage +
               // The parenthesis matters here because in general this is
               // counting on overflow of unsigned arithmetic to leave
               // a potentially smaller file offset than base value.  This
               // matters for 32 bit cores.
               (range._base + range._value._adjustToFileOffset);
      }
    }

    Offset Base() { return _oneWayIterator->_base; }
    Offset Size() { return _oneWayIterator->_size; }
    Offset Limit() { return _oneWayIterator->_limit; }
    int Flags() { return _oneWayIterator->_value._flags; }

   private:
    OneWayIterator _oneWayIterator;
    const char *_fileImage;
  };

  typedef RangeIterator<typename RangeFileOffsetMapper::const_iterator>
      const_iterator;
  typedef RangeIterator<typename RangeFileOffsetMapper::const_reverse_iterator>
      const_reverse_iterator;

  struct NotMapped {
    NotMapped(Offset address) : _address(address) {}
    Offset _address;
  };

  class Reader {
   public:
    Reader(const VirtualAddressMap &map)
        : _map(map),
          _iterator(map.end()),
          _endIterator(map.end()),
          _image((const char *)0),
          _base(0),
          _limit(0) {}
    /*
     * This form, which throws an exception if the address is not mapped,
     * should be used only if the address is actually expected to be mapped,
     * so as to avoid rather expensive control flow by exception.
     */
    Offset ReadOffset(Offset address) {
      Offset readLimit = address + sizeof(Offset);
      if (readLimit < address) {  // wrap
        throw NotMapped(address);
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            throw NotMapped(address);
          }
        }
        if (readLimit > _limit) {
          throw NotMapped(address);
        }
      }
      return *((Offset *)(_image + (address - _base)));
    }

    /*
     * This form should be used whenever there is a significant chance that
     * the specified address will not be mapped.
     */
    Offset ReadOffset(Offset address, Offset defaultValue) {
      Offset readLimit = address + sizeof(Offset);
      if (readLimit < address) {  // wrap
        return defaultValue;
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            return defaultValue;
          }
        }
        if (readLimit > _limit) {
          return defaultValue;
        }
      }
      return *((Offset *)(_image + (address - _base)));
    }
    /*
     * This form, which throws an exception if the address is not mapped,
     * should be used only if the address is actually expected to be mapped,
     * so as to avoid rather expensive control flow by exception.
     */
    uint16_t ReadU16(Offset address) {
      Offset readLimit = address + sizeof(uint16_t);
      if (readLimit < address) {  // wrap
        throw NotMapped(address);
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            throw NotMapped(address);
          }
        }
        if (readLimit > _limit) {
          throw NotMapped(address);
        }
      }
      return *((uint16_t *)(_image + (address - _base)));
    }
    /*
     * This form should be used whenever there is a significant chance that
     * the specified address will not be mapped.
     */
    uint16_t ReadU16(Offset address, uint16_t defaultValue) {
      Offset readLimit = address + sizeof(uint16_t);
      if (readLimit < address) {  // wrap
        return defaultValue;
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            return defaultValue;
          }
        }
        if (readLimit > _limit) {
          return defaultValue;
        }
      }
      return *((uint16_t *)(_image + (address - _base)));
    }
    /*
     * This form, which throws an exception if the address is not mapped,
     * should be used only if the address is actually expected to be mapped,
     * so as to avoid rather expensive control flow by exception.
     */
    uint32_t ReadU32(Offset address) {
      Offset readLimit = address + sizeof(uint32_t);
      if (readLimit < address) {  // wrap
        throw NotMapped(address);
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            throw NotMapped(address);
          }
        }
        if (readLimit > _limit) {
          throw NotMapped(address);
        }
      }
      return *((uint32_t *)(_image + (address - _base)));
    }
    /*
     * This form should be used whenever there is a significant chance that
     * the specified address will not be mapped.
     */
    uint32_t ReadU32(Offset address, uint32_t defaultValue) {
      Offset readLimit = address + sizeof(uint32_t);
      if (readLimit < address) {  // wrap
        return defaultValue;
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            return defaultValue;
          }
        }
        if (readLimit > _limit) {
          return defaultValue;
        }
      }
      return *((uint32_t *)(_image + (address - _base)));
    }
    /*
     * This form, which throws an exception if the address is not mapped,
     * should be used only if the address is actually expected to be mapped,
     * so as to avoid rather expensive control flow by exception.
     */
    uint64_t ReadU64(Offset address) {
      Offset readLimit = address + sizeof(uint64_t);
      if (readLimit < address) {  // wrap
        throw NotMapped(address);
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            throw NotMapped(address);
          }
        }
        if (readLimit > _limit) {
          throw NotMapped(address);
        }
      }
      return *((uint64_t *)(_image + (address - _base)));
    }
    /*
     * This form should be used whenever there is a significant chance that
     * the specified address will not be mapped.
     */
    uint64_t ReadU64(Offset address, uint64_t defaultValue) {
      Offset readLimit = address + sizeof(uint64_t);
      if (readLimit < address) {  // wrap
        return defaultValue;
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            return defaultValue;
          }
        }
        if (readLimit > _limit) {
          return defaultValue;
        }
      }
      return *((uint64_t *)(_image + (address - _base)));
    }
    /*
     * This form, which throws an exception if the address is not mapped,
     * should be used only if the address is actually expected to be mapped,
     * so as to avoid rather expensive control flow by exception.
     */
    uint8_t ReadU8(Offset address) {
      Offset readLimit = address + sizeof(uint8_t);
      if (readLimit < address) {  // wrap
        throw NotMapped(address);
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            throw NotMapped(address);
          }
        }
        if (readLimit > _limit) {
          throw NotMapped(address);
        }
      }
      return *((uint8_t *)(_image + (address - _base)));
    }
    /*
     * This form should be used whenever there is a significant chance that
     * the specified address will not be mapped.
     */
    uint8_t ReadU8(Offset address, uint8_t defaultValue) {
      Offset readLimit = address + sizeof(uint8_t);
      if (readLimit < address) {  // wrap
        return defaultValue;
      }
      if (_base > address || _limit < readLimit) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            return defaultValue;
          }
        }
        if (readLimit > _limit) {
          return defaultValue;
        }
      }
      return *((uint8_t *)(_image + (address - _base)));
    }
    template <typename T>
    void Read(Offset address, T *valueRead) {
      if (_base > address || _limit < address + sizeof(T)) {
        _image = (char *)0;
        _base = 0;
        _limit = 0;
        _iterator = _map.find(address);
        if (_iterator != _endIterator) {
          _image = _iterator.GetImage();
          if (_image != (char *)0) {
            _base = _iterator.Base();
            _limit = _iterator.Limit();
          } else {
            throw NotMapped(address);
          }
        }
        if (address + sizeof(T) > _limit) {
          throw NotMapped(address);
        }
      }
      *valueRead = *((T *)(_image + (address - _base)));
    }

   private:
    const VirtualAddressMap &_map;
    typename VirtualAddressMap::const_iterator _iterator;
    typename VirtualAddressMap::const_iterator _endIterator;
    const char *_image;
    Offset _base;
    Offset _limit;
  };
  VirtualAddressMap(const FileImage &fileImage)
      : _fileImage(fileImage), _fileSize((Offset)(fileImage.GetFileSize())) {}

  ~VirtualAddressMap() {}

  const FileImage &GetFileImage() const { return _fileImage; }

  const_iterator begin() const {
    return const_iterator(_ranges.begin(), _fileImage.GetImage());
  }
  const_iterator end() const {
    return const_iterator(_ranges.end(), _fileImage.GetImage());
  }
  const_iterator find(Offset addr) const {
    return const_iterator(_ranges.find(addr), _fileImage.GetImage());
  }

  const_iterator lower_bound(Offset addr) const {
    return const_iterator(_ranges.lower_bound(addr), _fileImage.GetImage());
  }

  const_iterator upper_bound(Offset addr) const {
    return const_iterator(_ranges.upper_bound(addr), _fileImage.GetImage());
  }

  Offset FindMappedMemoryImage(Offset addr, const char **image) const {
    const_iterator it = find(addr);
    if (it != end()) {
      const char *rangeImage = it.GetImage();
      if (rangeImage != (const char *)0) {
        Offset discard = addr - it.Base();
        *image = rangeImage + discard;
        return it.Size() - discard;
      }
    }
    *image = (const char *)0;
    return 0;
  }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(_ranges.rbegin(), _fileImage.GetImage());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(_ranges.rend(), _fileImage.GetImage());
  }

  void AddRange(Offset rangeAddr, Offset rangeSize, Offset adjustToFileOffset,
                bool isMapped, bool hasKnownPermissions, bool readable,
                bool writable, bool executable) {
    // TODO: add error handling
    int flags = 0;
    if (isMapped) {
      flags |= RangeAttributes::IS_MAPPED;
    }
    if (hasKnownPermissions) {
      flags |= RangeAttributes::HAS_KNOWN_PERMISSIONS;
      if (readable) {
        flags |= RangeAttributes::IS_READABLE;
      }
      if (writable) {
        flags |= RangeAttributes::IS_WRITABLE;
      }
      if (executable) {
        flags |= RangeAttributes::IS_EXECUTABLE;
      }
    }

    Offset limit = rangeAddr + rangeSize + adjustToFileOffset;
    bool overlap = false;
    if (_fileSize >= limit) {
      // The entire range has an image in the file.
      overlap = !_ranges.MapRange(rangeAddr, rangeSize,
                                  RangeAttributes(adjustToFileOffset, flags));
    } else if (_fileSize <= rangeAddr + adjustToFileOffset) {
      // The entire image is missing due to truncation.
      flags |= RangeAttributes::IS_TRUNCATED;
      overlap = !_ranges.MapRange(rangeAddr, rangeSize,
                                  RangeAttributes(adjustToFileOffset, flags));
    } else {
      Offset missing = limit - _fileSize;
      Offset present = rangeSize - missing;
      overlap = !_ranges.MapRange(rangeAddr, present,
                                  RangeAttributes(adjustToFileOffset, flags));
      flags |= RangeAttributes::IS_TRUNCATED;
      if (!_ranges.MapRange(rangeAddr + present, missing,
                            RangeAttributes(adjustToFileOffset, flags))) {
        overlap = true;
      }
    }
    if (overlap) {
      // TODO complain about everlap
    }
  }

  // TODO: resolve error handling for references
  // TODO: handle 0-page omission where dump format allows it and it
  //       makes sense

 private:
  const FileImage &_fileImage;
  Offset _fileSize;
  RangeFileOffsetMapper _ranges;
};
}  // namespace chap
