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

#ifndef SHARE_GC_Z_ZBYTE_HPP
#define SHARE_GC_Z_ZBYTE_HPP

#include "metaprogramming/isIntegral.hpp"
#include "metaprogramming/enableIf.hpp"
#include "metaprogramming/isSame.hpp"
#include "metaprogramming/removeCV.hpp"
#include "utilities/debug.hpp"

// https://en.cppreference.com/w/cpp/types/byte

enum class zbyte : unsigned char {};

template <typename IntegerType, ENABLE_IF(IsIntegral<IntegerType>::value)>
constexpr IntegerType to_integer(zbyte b) {
  return static_cast<IntegerType>(b);
}

template <typename IntegerType, ENABLE_IF(IsIntegral<IntegerType>::value)>
constexpr zbyte& operator<<=(zbyte& b, IntegerType shift) {
  return b = zbyte(static_cast<unsigned char>(b) << shift);
}

template <class IntegerType, ENABLE_IF(IsIntegral<IntegerType>::value)>
constexpr zbyte& operator>>=(zbyte& b, IntegerType shift) {
  return b = zbyte(static_cast<unsigned char>(b) >> shift);
}

template <class IntegerType, ENABLE_IF(IsIntegral<IntegerType>::value)>
constexpr zbyte operator<<(zbyte b, IntegerType shift) {
  return zbyte(static_cast<unsigned char>(b) << shift);
}

template <class IntegerType, ENABLE_IF(IsIntegral<IntegerType>::value)>
constexpr zbyte operator>>(zbyte b, IntegerType shift) {
  return zbyte(static_cast<unsigned char>(b) >> shift);
}

constexpr zbyte& operator|=(zbyte& l, zbyte r) {
  return l = zbyte(static_cast<unsigned char>(l) | static_cast<unsigned char>(r));
}

constexpr zbyte& operator&=(zbyte& l, zbyte r) {
  return l = zbyte(static_cast<unsigned char>(l) & static_cast<unsigned char>(r));
}

constexpr zbyte& operator^=(zbyte& l, zbyte r) {
  return l = zbyte(static_cast<unsigned char>(l) ^ static_cast<unsigned char>(r));
}

constexpr zbyte operator|(zbyte l, zbyte r) {
  return zbyte(static_cast<unsigned char>(l) | static_cast<unsigned char>(r));
}

constexpr zbyte operator&(zbyte l, zbyte r) {
  return zbyte(static_cast<unsigned char>(l) & static_cast<unsigned char>(r));
}

constexpr zbyte operator^(zbyte l, zbyte r) {
  return zbyte(static_cast<unsigned char>(l) ^ static_cast<unsigned char>(r));
}

constexpr zbyte operator~(zbyte b)  {
  return zbyte(~static_cast<unsigned char>(b));
}

// Non standard make helper as in C++ 14 we cannot create a `zbyte b{N}`

template<typename UnsignedCharEquivalent,
         ENABLE_IF(IsSame<unsigned char,
                          typename RemoveCV<UnsignedCharEquivalent>::type>::value)>
constexpr zbyte make_zbyte(UnsignedCharEquivalent uc) {
  return static_cast<zbyte>(uc);
}

template<int N>
constexpr zbyte make_zbyte() {
  STATIC_ASSERT(N >= 0 && N < 256);
  return static_cast<zbyte>(N);
}

#endif // SHARE_GC_Z_ZBYTE_HPP
