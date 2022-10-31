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

#ifndef SHARE_GC_Z_ZSPAN_HPP
#define SHARE_GC_Z_ZSPAN_HPP

#include "gc/z/zByte.hpp"
#include "metaprogramming/enableIf.hpp"
#include "metaprogramming/isConst.hpp"
#include "metaprogramming/primitiveConversions.hpp"
#include "metaprogramming/removeCV.hpp"
#include "utilities/globalDefinitions.hpp"

// https://en.cppreference.com/w/cpp/container/span/span

constexpr size_t dynamic_extent = -1;

template<typename T, size_t Extent = dynamic_extent>
class ZSpan {
public:
  using element_type     = T;
  using value_type       = typename RemoveCV<T>::type;
  using size_type        = size_t;
  using pointer          = T*;
  using const_pointer    = const T*;
  using reference        = T&;
  using const_reference  = const T&;
  using difference_type  = ptrdiff_t;
  /* No general iterator support yet
  using iterator         = ...;
  using reverse_iterator = ...;
  */

  static constexpr size_t extent = Extent;

  template<size_t ExtentValue = extent, ENABLE_IF(ExtentValue == 0 || ExtentValue == dynamic_extent)>
  constexpr ZSpan() : _data_holder(nullptr, ExtentHolder<0>()) {}

  /* No general iterator support yet
  template< class It >
  explicit(extent != dynamic_extent)
  constexpr ZSpan( It first, size_type count );
  */
  template<size_t ExtentValue = extent, ENABLE_IF(ExtentValue != dynamic_extent)>
  constexpr explicit ZSpan(pointer first, size_type count)
    : _data_holder(first, count) {
    precond(count == extent);
  };

  template<size_t ExtentValue = extent, ENABLE_IF(ExtentValue == dynamic_extent)>
  constexpr ZSpan(pointer first, size_type count)
    : _data_holder(first, count) {};

  /* No general iterator support yet
  template< class It, class End >
  explicit(extent != std::dynamic_extent)
  constexpr ZSpan( It first, End last );
  */
  template<size_t ExtentValue = extent, ENABLE_IF(ExtentValue != dynamic_extent)>
  constexpr ZSpan(pointer first, pointer last)
    : _data_holder(first, PrimitiveConversions::cast<size_type>(last - first)) {
    precond(last - first == PrimitiveConversions::cast<difference_type>(extent));
  };

  template<size_t ExtentValue = extent, ENABLE_IF(ExtentValue == dynamic_extent)>
  constexpr explicit ZSpan(pointer first, pointer last)
    : _data_holder(first, PrimitiveConversions::cast<size_type>(last - first)) {};

  template<size_t N>
  constexpr ZSpan(element_type (&arr)[N])
    : _data_holder(arr, ExtentHolder<N>()) {}

  /* No std::array equivelent yet
  template< class U, std::size_t N >
  constexpr ZSpan( std::array<U, N>& arr )

  template< class U, std::size_t N >
  constexpr ZSpan( const std::array<U, N>& arr )
  */

  /* No Range / Container support
  template<class R>
  explicit(extent != std::dynamic_extent)
  constexpr ZSpan(R&& range);
  */

  template<typename U, size_t N, ENABLE_IF(extent != dynamic_extent && N == dynamic_extent)>
  explicit constexpr ZSpan(const ZSpan<U, N>& source)
    : _data_holder(source.data(), ExtentHolder<N>(source.size())) {}

  template<typename U, size_t N, ENABLE_IF(extent == dynamic_extent || N != dynamic_extent)>
  constexpr ZSpan(const ZSpan<U, N>& source)
    : _data_holder(source.data(), ExtentHolder<N>(source.size())) {}

  constexpr ZSpan(const ZSpan& other) = default;
  constexpr ZSpan& operator=(const ZSpan& other) = default;
  ~ZSpan() = default;

  /* No general iterator support yet
  constexpr iterator begin() const;
  constexpr iterator end() const;
  constexpr reverse_iterator rbegin() const;
  constexpr reverse_iterator rend() const;
  */


  constexpr reference front() const {
    return operator[](0);
  }

  constexpr reference back() const {
    precond(size() > 0);
    return operator[](size() - 1);
  }

  constexpr reference operator[](size_type idx) const {
    precond(idx < size());
    return data()[idx];
  }

  constexpr pointer data() const {
    return _data_holder.data();
  }

  constexpr size_type size() const {
    return _data_holder.size();
  }

  constexpr size_type size_bytes() const {
    return size() * sizeof(element_type);
  }

  constexpr bool empty() const {
    return size() == 0;
  }

  // TODO(Axel): add static assertion when extent is known
  //             template <ENABLE_IF(extent != dynamic_extent)>
  //             template <ENABLE_IF(extent == dynamic_extent)>
  template <size_t Count>
  constexpr ZSpan<element_type, Count> first() const {
    precond(Count <= size());
    STATIC_ASSERT(Count <= extent);
    return ZSpan<element_type, Count>{data(), Count};
  }

  constexpr ZSpan<element_type> first(size_type count) const {
    precond(count <= size());
    return {data(), count};
  }

  // TODO(Axel): add static assertion when extent is known
  //             template <ENABLE_IF(extent != dynamic_extent)>
  //             template <ENABLE_IF(extent == dynamic_extent)>
  template <size_t Count>
  constexpr ZSpan<element_type, Count> last() const {
    precond(Count <= size());
    STATIC_ASSERT(Count <= extent);
    return ZSpan<element_type, Count>{data() + (size() - Count), Count};
  }

  constexpr ZSpan<element_type> last(size_type count) const {
    precond(count <= size());
    return {data() + (size() - count), count};
  }

private:
  template <size_t ExtentValue>
  class ExtentHolder {
  public:
    using size_type = size_t;

    constexpr ExtentHolder() = default;

    constexpr explicit ExtentHolder(ExtentHolder<dynamic_extent> extent) {
      precond(extent.size() == ExtentValue);
    }

    constexpr explicit ExtentHolder(size_type size) {
      precond(size == ExtentValue);
    }

    constexpr size_type size() const {
      return ExtentValue;
    }

  private:
    static constexpr size_type _size = ExtentValue;
  };

  template <>
  class ExtentHolder<dynamic_extent> {
  public:
    using size_type = size_t;

    template <size_type Other>
    constexpr explicit ExtentHolder(ExtentHolder<Other> extent)
      : _size(extent.size()) {}

    constexpr explicit ExtentHolder(size_type size) : _size(size) {
      precond(size != dynamic_extent);
    }

    constexpr size_type size() const {
      return _size;
    }

  private:
    size_type _size;
  };

  template<size_type Offset, size_type Count>
  struct SelectSubSpanType {
    static constexpr size_type extent = Count != dynamic_extent
        ? Count                       : Extent != dynamic_extent
        ? Extent - Offset
        : dynamic_extent;
    using type = ZSpan<element_type, extent>;
  };

  template<typename ExtentHolder>
  class DataHolder : public ExtentHolder {
  public:
    template <class OtherExtentHolder>
    constexpr DataHolder(pointer data, OtherExtentHolder extent)
      : ExtentHolder(extent), _data(data) {
      precond(data != nullptr || ExtentHolder::size() == 0);
    }

    constexpr pointer data() const { return _data; }

  private:
    pointer _data;
  };

  DataHolder<ExtentHolder<Extent>> _data_holder;

public:
  // TODO(Axel): add static assertion when extent is known
  //             template <ENABLE_IF(extent != dynamic_extent)>
  //             template <ENABLE_IF(extent == dynamic_extent)>
  template<size_t Offset, size_t Count = dynamic_extent>
  constexpr typename SelectSubSpanType<Offset, Count>::type subspan() const {
    using SubSpanType = typename SelectSubSpanType<Offset, Count>::type;
    postcond(Offset <= size());
    STATIC_ASSERT(Offset <= extent);
    postcond(Count == dynamic_extent || (Count <= size() - Offset));
    STATIC_ASSERT(Count == dynamic_extent || (Count <= extent - Offset));
    return SubSpanType{data() + Offset, Count == dynamic_extent ? size() - Offset : Count};
  }

  constexpr ZSpan<element_type> subspan(size_type Offset, size_type Count = dynamic_extent) const {
    postcond(Offset <= size());
    postcond(Count == dynamic_extent || (Count <= size() - Offset));
    return {data() + Offset, Count == dynamic_extent ? size() - Offset : Count};
  }
};

// Technically UB as zbyte is not std::byte

template<typename T, size_t N>
ZSpan<const zbyte, N == dynamic_extent ? dynamic_extent : N * sizeof(T)> as_bytes(ZSpan<T, N> s) {
    STATIC_ASSERT(N == dynamic_extent || N < dynamic_extent / sizeof(T));
    using ZSpanType = ZSpan<const zbyte, N == dynamic_extent ? dynamic_extent : N * sizeof(T)>;
    return ZSpanType{reinterpret_cast<const zbyte*>(s.data()), s.size_bytes()};
}

template<typename T, size_t N, ENABLE_IF(!IsConst<T>::value)>
ZSpan<zbyte, N == dynamic_extent ? dynamic_extent : N * sizeof(T)> as_writable_bytes(ZSpan<T, N> s) {
    STATIC_ASSERT(N == dynamic_extent || N < dynamic_extent / sizeof(T));
    using ZSpanType = ZSpan<zbyte ,N == dynamic_extent ? dynamic_extent : N * sizeof(T)>;
    return ZSpanType{reinterpret_cast<zbyte*>(s.data()), s.size_bytes()};
}

#endif // SHARE_GC_Z_ZSPAN_HPP