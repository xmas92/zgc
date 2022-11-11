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

#ifndef SHARE_GC_Z_ZCARRAY_HPP
#define SHARE_GC_Z_ZCARRAY_HPP

#include "metaprogramming/removeCV.hpp"
#include "gc/z/zReverseIterator.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"

// https://en.cppreference.com/w/cpp/container/array



namespace {
  template<typename T, size_t N>
  struct ZCArrayIterator {
    /* No concept support yet
    using iterator_concept = std::contiguous_iterator_tag;
    */
    /* No stl support yet
    using iterator_category = std::random_access_iterator_tag;
    */
    using value_type = typename RemoveCV<T>::type;
    using difference_type = ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    constexpr operator ZCArrayIterator<const T, N>() {
      return {_ptr, _position};
    }

    constexpr reference operator*() const {
      precond(_position >= 0);
      precond(_position < N);
      return _ptr[_position];
    }

    constexpr pointer operator->() const {
      precond(_position >= 0);
      precond(_position < N);
      return &_ptr[_position];
    }

    constexpr ZCArrayIterator& operator++() {
      precond(_position >= 0);
      precond(_position < N);
      ++_position;
      return *this;
    }

    constexpr ZCArrayIterator operator++(int) {
      auto temp = *this;
      operator++();
      return temp;
    }

    constexpr ZCArrayIterator& operator--() {
      precond(_position > 0);
      precond(_position <= N);
      --_position;
      return *this;
    }

    constexpr ZCArrayIterator operator--(int) {
      auto temp = *this;
      operator--();
      return temp;
    }

    constexpr ZCArrayIterator& operator+=(const difference_type offset) {
      precond(_position + offset >= 0);
      precond(_position + offset <= N);
      _position += offset;
      return *this;
    }

    constexpr ZCArrayIterator operator+(const difference_type offset) const {
      auto temp = *this;
      temp += offset;
      return temp;
    }

    friend constexpr ZCArrayIterator& operator+(const difference_type offset, ZCArrayIterator& rhs) {
      rhs += offset;
      return rhs;
    }

    constexpr ZCArrayIterator& operator-=(const difference_type offset) {
      precond(_position - offset >= 0);
      precond(_position - offset <= N);
      _position -= offset;
      return *this;
    }

    constexpr ZCArrayIterator operator-(const difference_type offset) const {
      auto temp = *this;
      temp -= offset;
      return temp;
    }

    constexpr difference_type operator-(const ZCArrayIterator& rhs) const {
      precond(_ptr == rhs._ptr);
      return _position - rhs._position;
    }

    constexpr reference operator[](const difference_type offset) const {
      return *(operator+(offset));
    }

    constexpr bool operator==(const ZCArrayIterator& rhs) const {
      precond(_ptr == rhs._ptr);
      return _position == rhs._position;
    }

    constexpr bool operator!=(const ZCArrayIterator& rhs) const {
      return !(*this == rhs);
    }

    constexpr bool operator<(const ZCArrayIterator& rhs) const {
      precond(_ptr == rhs._ptr);
      return _position < rhs._position;
    }

    constexpr bool operator>(const ZCArrayIterator& rhs) const {
      precond(_ptr == rhs._ptr);
      return _position > rhs._position;
    }

    constexpr bool operator<=(const ZCArrayIterator& rhs) const {
      return !(*this > rhs);
    }

    constexpr bool operator>=(const ZCArrayIterator& rhs) const {
      return !(*this < rhs);
    }

    pointer _ptr = nullptr;
    size_t _position = 0;
  };
} // namespace

template<typename T, size_t N>
struct ZCArray {
  using value_type =	T;
  using size_type =	size_t;
  using difference_type =	ptrdiff_t;
  using reference =	value_type&;
  using const_reference =	const value_type&;
  using pointer =	value_type*;
  using const_pointer =	const value_type*;
  using iterator = ZCArrayIterator<T, N>;
  using const_iterator = ZCArrayIterator<const T, N>;
  using reverse_iterator = ZReveserIterator<iterator>;
  using const_reverse_iterator = ZReveserIterator<const_iterator>;

  // TODO(Axel): Should probably check and crash in release
  constexpr reference at(size_type pos) {
    guarantee(pos < N, "pos < N");
    return _data[pos];

  }

  constexpr const_reference at( size_type pos ) const {
    guarantee(pos < N, "pos < N");
    return _data[pos];
  }

  constexpr reference operator[]( size_type pos ) {
    precond(pos < N);
    return _data[pos];

  }

  constexpr const_reference operator[]( size_type pos ) const {
    precond(pos < N);
    return _data[pos];

  }

  constexpr reference front() {
    return _data[0];
  }

  constexpr const_reference front() const {
    return _data[0];
  }

  constexpr reference back() {
    return _data[N-1];
  }

  constexpr const_reference back() const {
    return _data[N-1];
  }

  constexpr T* data() {
    return _data;
  }

  constexpr const T* data() const {
    return _data;
  }

  constexpr iterator begin() {
    return {data(), 0};
  }

  constexpr const_iterator begin() const {
    return {data(), 0};
  }

  constexpr const_iterator cbegin() const {
    return {data(), 0};
  }

  constexpr iterator end() {
    return {data(), N};
  }

  constexpr const_iterator end() const {
    return {data(), N};
  }

  constexpr const_iterator cend() const {
    return {data(), N};
  }

  constexpr reverse_iterator rbegin() {
    return reverse_iterator{end()};
  }

  constexpr const_reverse_iterator rbegin() const {
    return const_reverse_iterator{end()};
  }

  constexpr const_reverse_iterator crbegin() const {
    return const_reverse_iterator{cend()};
  }

  constexpr reverse_iterator rend() {
    return reverse_iterator{begin()};
  }

  constexpr const_reverse_iterator rend() const {
    return const_reverse_iterator{begin()};
  }

  constexpr const_reverse_iterator crend() const {
    return const_reverse_iterator{cbegin()};
  }

  // Add [[nodiscard]] C++17
  constexpr bool empty() const {
    return false;
  }

  constexpr size_type size() const {
    return N;
  }

  constexpr size_type max_size() const {
    return N;
  }

  // TODO(Axel): Use general algorithms
  constexpr void fill(const T& value) {
    for (size_type i = 0; i < N; ++i) {
      _data[i] = value;
    }
  }

  /* TODO(Axel): Requires std::swap, how should this be handled
  constexpr void swap(array& other)
  */

  T _data[N];
};


template<typename T>
struct ZCArray<T, 0> {
  using value_type =	T;
  using size_type =	size_t;
  using difference_type =	ptrdiff_t;
  using reference =	value_type&;
  using const_reference =	const value_type&;
  using pointer =	value_type*;
  using const_pointer =	const value_type*;
  using iterator = ZCArrayIterator<T, 0>;
  using const_iterator = ZCArrayIterator<const T, 0>;
  using reverse_iterator = ZReveserIterator<iterator>;
  using const_reverse_iterator = ZReveserIterator<const_iterator>;

  // TODO(Axel): Add [[noreturn]] attribute if error_is_suppressed is ever changed
  constexpr reference at(size_type) {
    ShouldNotCallThis();
    return *data();
  }

  // TODO(Axel): Add [[noreturn]] attribute if error_is_suppressed is ever changed
  constexpr const_reference at(size_type) const {
    ShouldNotCallThis();
    return *data();
  }

  constexpr reference operator[](size_type) {
    ShouldNotCallThis();
    return *data();

  }

  constexpr const_reference operator[](size_type) const {
    ShouldNotCallThis();
    return *data();

  }

  constexpr reference front() {
    ShouldNotCallThis();
    return *data();
  }

  constexpr const_reference front() const {
    ShouldNotCallThis();
    return*data();
  }

  constexpr reference back() {
    ShouldNotCallThis();
    return *data();
  }

  constexpr const_reference back() const {
    ShouldNotCallThis();
    return *data();
  }

  constexpr T* data() {
    return nullptr;
  }

  constexpr const T* data() const {
    return nullptr;
  }

  constexpr iterator begin() {
    return {};
  }

  constexpr const_iterator begin() const {
    return {};
  }

  constexpr const_iterator cbegin() const {
    return {};
  }

  constexpr iterator end() {
    return {};
  }

  constexpr const_iterator end() const {
    return {};
  }

  constexpr const_iterator cend() const {
    return {};
  }

  constexpr reverse_iterator rbegin() {
    return {};
  }

  constexpr const_reverse_iterator rbegin() const {
    return {};
  }

  constexpr const_reverse_iterator crbegin() const {
    return {};
  }

  constexpr reverse_iterator rend() {
    return {};
  }

  constexpr const_reverse_iterator rend() const {
    return {};
  }

  constexpr const_reverse_iterator crend() const {
    return {};
  }

  // Add [[nodiscard]] C++17
  constexpr bool empty() const {
    return true;
  }

  constexpr size_type size() const {
    return 0;
  }

  constexpr size_type max_size() const {
    return 0;
  }

  constexpr void fill(const T& value) {}

  /* TODO(Axel): Requires std::swap, how should this be handled
  constexpr void swap(array& other)
  */
};

template<size_t I, typename T, size_t N >
constexpr T& get(ZCArray<T,N>& a) {
  STATIC_ASSERT(I < N);
  return a._data[I];
}

template<size_t I, typename T, size_t N >
constexpr T&& get(ZCArray<T,N>& a) {
  STATIC_ASSERT(I < N);
  return a._data[I];
}

template<size_t I, typename T, size_t N >
constexpr const T& get(ZCArray<T,N>& a) {
  STATIC_ASSERT(I < N);
  return a._data[I];
}

template<size_t I, typename T, size_t N >
constexpr const T&& get(ZCArray<T,N>& a) {
  STATIC_ASSERT(I < N);
  return a._data[I];
}

// TODO(Axel): Use general algorithms
#define ZCArraySpaceshipOperator(lhs, rhs)              \
  [&]() -> int {                                        \
    using size_type = typename ZCArray<T,N>::size_type; \
    if (lhs.data() == rhs.data()) { return 0; }         \
    for (size_type i = 0; i < N; ++i) {                 \
      if (lhs[i] != rhs[i]) {                           \
        return lhs[i] < rhs[i] ? -1 : 1;                \
      }                                                 \
    }                                                   \
    return 0;                                           \
  }()

template<typename T, size_t N>
constexpr bool operator==(const ZCArray<T,N>& lhs, const ZCArray<T,N>& rhs) {
  const auto order = ZCArraySpaceshipOperator(lhs, rhs);
  return order == 0;
}

template<typename T, size_t N>
constexpr bool operator!=(const ZCArray<T,N>& lhs, const ZCArray<T,N>& rhs) {
  const auto order = ZCArraySpaceshipOperator(lhs, rhs);
  return order != 0;
}

template<typename T, size_t N>
constexpr bool operator<(const ZCArray<T,N>& lhs, const ZCArray<T,N>& rhs) {
  const auto order = ZCArraySpaceshipOperator(lhs, rhs);
  return order < 0;
}

template<typename T, size_t N>
constexpr bool operator>(const ZCArray<T,N>& lhs, const ZCArray<T,N>& rhs) {
  const auto order = ZCArraySpaceshipOperator(lhs, rhs);
  return order > 0;
}

template<typename T, size_t N>
constexpr bool operator<=(const ZCArray<T,N>& lhs, const ZCArray<T,N>& rhs) {
  const auto order = ZCArraySpaceshipOperator(lhs, rhs);
  return order <= 0;
}

template<typename T, size_t N>
constexpr bool operator>=(const ZCArray<T,N>& lhs, const ZCArray<T,N>& rhs) {
  const auto order = ZCArraySpaceshipOperator(lhs, rhs);
  return order >= 0;
}

#undef ZCArraySpaceshipOperator

/* TODO(Axel): * Are these helpers needed?
               * Requires std::is_constructible and std::is_move_constructible
               * Fix move semantics for Hotspot :)
#include "metaprogramming/removeCV.hpp"
template<class T, size_t N>
constexpr ZCArray<typename RemoveCV<T>::type, N> to_array(T (&a)[N]);
template<class T, size_t N>
constexpr ZCArray<typename RemoveCV<T>::type, N> to_array(T (&&a)[N]);
*/

#endif // SHARE_GC_Z_ZCARRAY_HPP
