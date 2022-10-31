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

#include "utilities/globalDefinitions.hpp"
#include "utilities/debug.hpp"

// https://en.cppreference.com/w/cpp/container/array

template<typename T, size_t N>
struct ZCArray {
  using value_type =	T;
  using size_type =	size_t;
  using difference_type =	ptrdiff_t;
  using reference =	value_type&;
  using const_reference =	const value_type&;
  using pointer =	value_type*;
  using const_pointer =	const value_type*;
  /* No general iterator support yet
  using iterator =
  using const_iterator =
  using reverse_iterator =
  using const_reverse_iterator =
  */

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

  /* No general iterator support yet
  constexpr iterator begin();
  constexpr const_iterator begin() const;
  constexpr const_iterator cbegin() const;
  constexpr iterator end();
  constexpr const_iterator end() const;
  constexpr const_iterator cend() const;
  constexpr reverse_iterator rbegin();
  constexpr const_reverse_iterator rbegin() const;
  constexpr const_reverse_iterator crbegin() const;
  constexpr reverse_iterator rend();
  constexpr const_reverse_iterator rend() const;
  constexpr const_reverse_iterator crend() const;
  */

  [[nodiscard]] constexpr bool empty() const {
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
  /* No general iterator support yet
  using iterator =
  using const_iterator =
  using reverse_iterator =
  using const_reverse_iterator =
  */

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

  /* No general iterator support yet
  constexpr iterator begin();
  constexpr const_iterator begin() const;
  constexpr const_iterator cbegin() const;
  constexpr iterator end();
  constexpr const_iterator end() const;
  constexpr const_iterator cend() const;
  constexpr reverse_iterator rbegin();
  constexpr const_reverse_iterator rbegin() const;
  constexpr const_reverse_iterator crbegin() const;
  constexpr reverse_iterator rend();
  constexpr const_reverse_iterator rend() const;
  constexpr const_reverse_iterator crend() const;
  */

  [[nodiscard]] constexpr bool empty() const {
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