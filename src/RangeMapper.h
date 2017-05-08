// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <functional>
#include <map>
namespace chap {
template <class Off, class T>
class RangeMapper {
 public:
  typedef Off Offset;
  typedef T ValueType;
  typedef std::map<Offset, std::pair<Offset, ValueType> > Map;

  typedef typename Map::iterator MapIterator;
  typedef typename Map::const_iterator MapConstIterator;
  typedef typename Map::const_reverse_iterator MapConstReverseIterator;

  struct Range {
    Range() : _limit(0), _size(0), _base(0) {}
    void Set(Offset limit, Offset size, ValueType value) {
      _limit = limit;
      _size = size;
      _value = value;
      _base = limit - size;
    }
    Offset _limit;
    Offset _size;
    ValueType _value;
    Offset _base;
  };

  template <class OneWayIterator>
  class RangeIterator {
   public:
    RangeIterator(const OneWayIterator& it)
        : _mapIterator(it), _rangeRefreshNeeded(true) {}

    RangeIterator(const RangeIterator& other)
        : _mapIterator(other._mapIterator), _rangeRefreshNeeded(true) {}

    RangeIterator& operator=(const RangeIterator& other) {
      _mapIterator = other._mapIterator;
      _rangeRefreshNeeded = true;
      return *this;
    }

    bool operator==(const RangeIterator& other) const {
      return _mapIterator == other._mapIterator;
    }

    bool operator!=(const RangeIterator& other) const {
      return _mapIterator != other._mapIterator;
    }

    RangeIterator& operator++() {
      ++_mapIterator;
      _rangeRefreshNeeded = true;
      return *this;
    }

    const Range* operator->() {
      if (_rangeRefreshNeeded) {
        _currentRange.Set(_mapIterator->first, _mapIterator->second.first,
                          _mapIterator->second.second);
        _rangeRefreshNeeded = false;
      }
      return &_currentRange;
    }
    const Range& operator*() {
      if (_rangeRefreshNeeded) {
        _currentRange.Set(_mapIterator->first, _mapIterator->second.first,
                          _mapIterator->second.second);
        _rangeRefreshNeeded = false;
      }
      return _currentRange;
    }

   private:
    Range _currentRange;
    OneWayIterator _mapIterator;
    bool _rangeRefreshNeeded;
  };

  typedef RangeIterator<MapConstIterator> const_iterator;
  typedef RangeIterator<MapConstReverseIterator> const_reverse_iterator;

  const_iterator begin() const {
    return RangeIterator<MapConstIterator>(this->_map.begin());
  }

  const_iterator end() const {
    return RangeIterator<MapConstIterator>(this->_map.end());
  }

  const_reverse_iterator rbegin() const {
    return RangeIterator<MapConstReverseIterator>(this->_map.rbegin());
  }

  const_reverse_iterator rend() const {
    return RangeIterator<MapConstReverseIterator>(this->_map.rend());
  }

  bool MapRange(Offset rangeBase, Offset rangeSize, ValueType value) {
    if (rangeSize == 0) {
      return true;
    }
    Offset rangeLimit = rangeBase + rangeSize;

    MapIterator it = _map.lower_bound(rangeBase);
    if (it != _map.end() && it->first - it->second.first <= rangeLimit) {
      /*
       * This overlaps or touches an existing range.  Overlapping is not
       * allowed but touching ranges provide the opportunity to coallesce.
       */
      if (it->first - it->second.first == rangeLimit) {
        /*
         * There is a touching range just after this one and no overlapping
         * range before it.
         */
        if (it->second.second == value) {
          /*
           * The touching range has the same value and would have the same
           * key in the map so we can coallesce in the existing node
           * by changing the range size.
           */
          it->second.first += rangeSize;
          return true;
        }
      } else if (it->first == rangeBase) {
        /*
         * There is a touching range just before this one.  This provides
         * an opportunity to coallesce but first we must check that there
         * is no overlap with another existing range after that one.
         */
        MapIterator itNext = it;
        itNext++;
        if (itNext != _map.end() &&
            itNext->first - itNext->second.first < rangeLimit) {
          // There is overlap with the range after that one.
          return false;
        }
        if (it->second.second == value) {
          // The ranges can be coallesced into a range with a new end.
          rangeSize += it->second.first;
          _map.erase(it);
        }
      } else {
        /*
         * There is overlap between the start of the existing range and
         * the end of the new one.
         */
        return false;
      }
    }
    _map[rangeLimit] = std::make_pair(rangeSize, value);
    return true;
  }

  void UnmapRange(Offset rangeBase, Offset rangeSize) {
    if (rangeSize == 0) {
      return;
    }
    Offset rangeLimit = rangeBase + rangeSize;

    MapIterator it = _map.lower_bound(rangeBase);
    while (it != _map.end() && it->first - it->second.first < rangeLimit) {
      Offset limit = it->first;
      /*
       * The range to remove overlaps or immediately follows an existing
       * range.
       */
      if (limit == rangeBase) {
        /*
         * There is a touching range just before the range to remove.
         * Just advance to see if the next range overlaps.
         */
        ++it;
      } else {
        /*
         * There is overlap between the existing range and
         * the end of the range to remove.
         */
        Offset base = limit - it->second.first;
        if (base < rangeBase) {
          /*
           * Part of the existing range precedes the range to remove
           * and now requires a new range.
           */
          _map[rangeBase] = std::make_pair(rangeBase - base, it->second.second);
        }
        if (limit <= rangeLimit) {
          /*
           * The existing range does not extend past the old range to
           * remove.
           */
          MapIterator itToErase = it;
          ++it;
          _map.erase(itToErase);
        } else {
          /*
           * The existing range extends past the range to remove.
           */
          it->second.first = limit - rangeLimit;
          break;
        }
      }
    }
  }

  const_iterator find(Offset member) const {
    MapConstIterator it = _map.upper_bound(member);
    if (it != _map.end()) {
      if (member < it->first - it->second.first) {
        // member is less than base (limit - size)
        it = _map.end();
      }
    }
    return const_iterator(it);
  }

  /*
   * This returns an iterator to the first range with limit not before the
   * given member, or an iterator to the end if no such range exists.
   */
  const_iterator lower_bound(Offset member) const {
    return const_iterator(_map.upper_bound(member));
  }

  /*
   * This returns an iterator to the first range with limit after the given
   * member, or an iterator to the end if no such range exists.
   */
  const_iterator upper_bound(Offset member) const {
    return const_iterator(_map.upper_bound(member));
  }

  /*
   * If a range containing the given member exists, return true and
   * set rangeBase, rangeSize and valueType accordingly.  If not, return
   * false and leave rangeBase, rangeSize and valueType untouched.
   */

  bool FindRange(Offset member, Offset& rangeBase, Offset& rangeSize,
                 ValueType& value) const {
    MapConstIterator it = _map.upper_bound(member);
    if (it != _map.end()) {
      Offset foundRangeSize = it->second.first;
      Offset foundRangeBase = it->first - foundRangeSize;
      if (foundRangeBase <= member) {
        rangeBase = foundRangeBase;
        rangeSize = foundRangeSize;
        value = it->second.second;
        return true;
      }
    }
    return false;
  }

  typedef std::function<bool(Offset, Offset, ValueType)> RangeVisitor;

  bool VisitRanges(RangeVisitor visitor) const {
    for (MapConstIterator it = _map.begin(); it != _map.end(); ++it) {
      if (visitor(it->first - it->second.first, it->second.first,
                  it->second.second)) {
        return true;
      }
    }
    return false;
  }

  bool VisitRangesBackwards(RangeVisitor visitor) const {
    for (MapConstReverseIterator it = _map.rbegin(); it != _map.rend(); ++it) {
      if (visitor(it->first - it->second.first, it->second.first,
                  it->second.second)) {
        return true;
      }
    }
    return false;
  }

 private:
  Map _map;
};

}  // namespace chap
