/*
 * Copyright (c) 2015, 2020, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "gc/z/zByte.hpp"
#include "utilities/globalDefinitions.hpp"
#include "unittest.hpp"

#ifndef PRODUCT


TEST(zbyte, to_integer) {
  const auto b = make_zbyte<0b10101010>();

  EXPECT_TRUE( to_integer<int8_t>(b) == (int8_t)0b10101010);
  EXPECT_TRUE(to_integer<int16_t>(b) == 0b10101010);
  EXPECT_TRUE(to_integer<int32_t>(b) == 0b10101010);
  EXPECT_TRUE(to_integer<int64_t>(b) == 0b10101010);

  EXPECT_TRUE( to_integer<uint8_t>(b) == 0b10101010);
  EXPECT_TRUE(to_integer<uint16_t>(b) == 0b10101010);
  EXPECT_TRUE(to_integer<uint32_t>(b) == 0b10101010);
  EXPECT_TRUE(to_integer<uint64_t>(b) == 0b10101010);
}
TEST(zbyte, construction) {
  // casts
  {
    const auto b = static_cast<zbyte>(1);
    EXPECT_TRUE(static_cast<unsigned char>(b) == 1);
  }
  {
    const auto b = zbyte(2);
    EXPECT_TRUE(static_cast<unsigned char>(b) == 2);
  }

  // make_zbyte helper
  {
    const unsigned char uc = 3;
    const auto b = make_zbyte(uc);
    EXPECT_TRUE(static_cast<unsigned char>(b) == 3);
  }
  {
    const auto b = make_zbyte<4>();
    EXPECT_TRUE(static_cast<unsigned char>(b) == 4);
  }
}

TEST(zbyte, bitwise_operators) {
  const auto b00 = make_zbyte<0x00>();
  const auto bAA = make_zbyte<0xAA>();
  const auto b55 = make_zbyte<0x55>();
  const auto bFF = make_zbyte<0xFF>();
  const auto bF0 = make_zbyte<0xF0>();
  const auto b0F = make_zbyte<0x0F>();

  // operator<<=,operator>>=
  {
    auto b = bAA;
    b >>= 1U;
    EXPECT_TRUE(b == b55);
    b <<= 1U;
    EXPECT_TRUE(b == bAA);
  }
  {
    auto b = bFF;
    b >>= 4U;
    EXPECT_TRUE(b == b0F);
    b <<= 4U;
    EXPECT_TRUE(b == bF0);
  }

  // operator<<,operator>>
  EXPECT_TRUE((bAA >> 1U) == b55);
  EXPECT_TRUE((b55 << 1U) == bAA);
  EXPECT_TRUE((bFF >> 4U) == b0F);
  EXPECT_TRUE((bFF << 4U) == bF0);

  // operator|=,operator&=,operator^=
  {
    auto b = b0F;
    b |= b00;
    EXPECT_TRUE(b == b0F);
    b |= bFF;
    EXPECT_TRUE(b == bFF);
  }

  {
    auto b = b0F;
    b &= bFF;
    EXPECT_TRUE(b == b0F);
    b &= b00;
    EXPECT_TRUE(b == b00);
  }

  {
    auto b = b0F;
    b ^= bFF;
    EXPECT_TRUE(b == bF0);
    b ^= b00;
    EXPECT_TRUE(b == bF0);
  }

  // operator|,operator&,operator^,operator~
  EXPECT_TRUE((b00 | b0F) == b0F);
  EXPECT_TRUE((b00 | bAA) == bAA);
  EXPECT_TRUE((bFF | b0F) == bFF);
  EXPECT_TRUE((bFF | bAA) == bFF);

  EXPECT_TRUE((b00 & b0F) == b00);
  EXPECT_TRUE((b00 & bAA) == b00);
  EXPECT_TRUE((bFF & b0F) == b0F);
  EXPECT_TRUE((bFF & bAA) == bAA);

  EXPECT_TRUE((b00 ^ b0F) == b0F);
  EXPECT_TRUE((b00 ^ bAA) == bAA);
  EXPECT_TRUE((bFF ^ b0F) == bF0);
  EXPECT_TRUE((bFF ^ bAA) == b55);

  EXPECT_TRUE((bFF ^ bFF) == b00);
  EXPECT_TRUE((bF0 ^ b0F) == bFF);

  EXPECT_TRUE(~bFF == b00);
  EXPECT_TRUE(~bF0 == b0F);
  EXPECT_TRUE(~bAA == b55);
}


TEST(zbyte, cppreference) {
  auto b = zbyte(42);
  EXPECT_TRUE(static_cast<unsigned char>(b) == 0b00101010);

  b <<= 1;
  EXPECT_TRUE(static_cast<unsigned char>(b) == 0b01010100);

  b >>= 1;
  EXPECT_TRUE(static_cast<unsigned char>(b) == 0b00101010);

  EXPECT_TRUE(static_cast<unsigned char>(b << 1) == 0b01010100);
  EXPECT_TRUE(static_cast<unsigned char>(b >> 1) == 0b00010101);

  b |= zbyte(0b11110000);
  EXPECT_TRUE(static_cast<unsigned char>(b) == 0b11111010);

  b &= zbyte(0b11110000);
  EXPECT_TRUE(static_cast<unsigned char>(b) == 0b11110000);

  b ^= zbyte(0b11111111);
  EXPECT_TRUE(static_cast<unsigned char>(b) == 0b00001111);
}

// zbyte should behave as char, unsigned char and std::byte with respect to aliasing
struct ZByteAliasingTest : public ::testing::Test {
  template<typename IntegralType, IntegralType i_value, unsigned char b_value>
  IntegralType with_reference(zbyte& b, IntegralType& i) {
    i = i_value;
    b = make_zbyte<b_value>();
    return i;
  }
  template<typename IntegralType, IntegralType i_value, unsigned char b_value>
  IntegralType with_pointer(zbyte* b, IntegralType& i) {
    i = i_value;
    *b = make_zbyte<b_value>();
    return i;
  }
};
TEST_F(ZByteAliasingTest, aliasing)
{
  // Reference
  {
    uint8_t i{0};
    auto& ref = reinterpret_cast<zbyte&>(i);
    const auto res = with_reference<decltype(i),1,2>(ref, i);
    EXPECT_TRUE(res == i);
    EXPECT_TRUE(res == 2);
  }
  {
    uint16_t i{0};
    auto& ref = reinterpret_cast<zbyte&>(i);
    const auto res = with_reference<decltype(i), (1 << BitsPerByte),2>(ref, i);
    EXPECT_TRUE(res == i);
    EXPECT_TRUE(res == (1 << BitsPerByte) + 2);
  }
  {
    uint16_t i{0};
    auto& ref = reinterpret_cast<zbyte&>(i);
    const auto res = with_reference<decltype(i),1,2>(ref, i);
    EXPECT_TRUE(res == i);
    EXPECT_TRUE(res == 2);
  }
  {
    uint16_t i{0};
    auto& ref = reinterpret_cast<zbyte*>(&i)[1];
    const auto res = with_reference<decltype(i),1,2>(ref, i);
    EXPECT_TRUE(res == i);
    EXPECT_TRUE(res == (2 << BitsPerByte) + 1);
  }

  // Pointer
  {
    uint8_t i{0};
    const auto& ref = reinterpret_cast<zbyte*>(&i);
    const auto res = with_pointer<decltype(i),1,2>(ref, i);
    EXPECT_TRUE(res == i);
    EXPECT_TRUE(res == 2);
  }
  {
    uint16_t i{0};
    const auto ptr = reinterpret_cast<zbyte*>(&i);
    const auto res = with_pointer<decltype(i),(1 << BitsPerByte),2>(ptr, i);
    EXPECT_TRUE(res == i);
    EXPECT_TRUE(res == (1 << BitsPerByte) + 2);
  }
  {
    uint16_t i{0};
    const auto ptr = reinterpret_cast<zbyte*>(&i);
    const auto res = with_pointer<decltype(i),1,2>(ptr, i);
    EXPECT_TRUE(res == i);
    EXPECT_TRUE(res == 2);
  }
  {
    uint16_t i{0};
    const auto ptr = (reinterpret_cast<zbyte*>(&i) + 1);
    const auto res = with_pointer<decltype(i),1,2>(ptr, i);
    EXPECT_TRUE(res == i);
    EXPECT_TRUE(res == (2 << BitsPerByte) + 1);
  }
}

#endif // PRODUCT
