/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifndef SHARE_GC_Z_ZREVERSEITERATOR_HPP
#define SHARE_GC_Z_ZREVERSEITERATOR_HPP

// https://en.cppreference.com/w/cpp/iterator/reverse_iterator

template<typename It>
class ZReveserIterator {
public:
  using iterator_type = It;
  /* only supports random_access_iterator_tag semantics at the moment
  using iterator_concept = ...;
  using iterator_category = ...;
  */
  using value_type = typename It::value_type;
  using difference_type = typename It::difference_type;
  using pointer = typename It::pointer;
  using reference = typename It::reference;

  constexpr ZReveserIterator() = default;
  constexpr explicit ZReveserIterator(iterator_type it) : _current(it) {};
  /*
  TODO(Axel): std::convertible_to
  template<class U, !IsSame<U, It>::value && std::convertible_to<const U&, It>>
  constexpr ZReveserIterator(const ZReveserIterator<U>& other);
  */

    constexpr reference operator*() const {
      return *(_current-1);
    }

    constexpr pointer operator->() const {
      return &operator*();
    }

    constexpr ZReveserIterator& operator++() {
      --_current;
      return *this;
    }

    constexpr ZReveserIterator operator++(int) {
      auto temp = *this;
      operator--();
      return temp;
    }

    constexpr ZReveserIterator& operator--() {
      ++_current;
      return *this;
    }

    constexpr ZReveserIterator operator--(int) {
      auto temp = *this;
      operator++();
      return temp;
    }

    constexpr ZReveserIterator& operator+=(const difference_type offset) {
      _current -= offset;
      return *this;
    }

    constexpr ZReveserIterator operator+(const difference_type offset) const {
      auto temp = *this;
      temp += offset;
      return temp;
    }

    friend constexpr ZReveserIterator& operator+(const difference_type offset, ZReveserIterator& rhs) {
      rhs += offset;
      return rhs;
    }

    constexpr ZReveserIterator& operator-=(const difference_type offset) {
      _current += offset;
      return *this;
    }

    constexpr ZReveserIterator operator-(const difference_type offset) const {
      auto temp = *this;
      temp -= offset;
      return temp;
    }

    constexpr difference_type operator-(const ZReveserIterator& rhs) const {
      return rhs._current - _current;
    }

    constexpr reference operator[](const difference_type offset) const {
      return *(operator+(offset));
    }

    constexpr bool operator==(const ZReveserIterator& rhs) const {
      return _current == rhs._current;
    }

    constexpr bool operator!=(const ZReveserIterator& rhs) const {
      return !(*this == rhs);
    }

    constexpr bool operator<(const ZReveserIterator& rhs) const {
      return _current > rhs._current;
    }

    constexpr bool operator>(const ZReveserIterator& rhs) const {
      return _current < rhs._current;
    }

    constexpr bool operator<=(const ZReveserIterator& rhs) const {
      return !(*this > rhs);
    }

    constexpr bool operator>=(const ZReveserIterator& rhs) const {
      return !(*this < rhs);
    }

protected:
  It _current;
};

#endif // SHARE_GC_Z_ZREVERSEITERATOR_HPP
