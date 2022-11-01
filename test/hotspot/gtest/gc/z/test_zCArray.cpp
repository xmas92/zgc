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
#include "gc/z/zCArray.hpp"
#include "utilities/debug.hpp"
#include "unittest.hpp"

#ifndef PRODUCT

TEST(ZCArray, construction_empty) {
  using T = int;
  {
    ZCArray<T, 0> a;
    EXPECT_TRUE(a.size() == 0);
    EXPECT_TRUE(a.max_size() == 0);
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(a.data() == nullptr);
  }
  {
    const ZCArray<T, 0> a;
    EXPECT_TRUE(a.size() == 0);
    EXPECT_TRUE(a.max_size() == 0);
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(a.data() == nullptr);
  }
  {
    ZCArray<T, 0> a{};
    EXPECT_TRUE(a.size() == 0);
    EXPECT_TRUE(a.max_size() == 0);
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(a.data() == nullptr);
  }
  {
    const ZCArray<T, 0> a{};
    EXPECT_TRUE(a.size() == 0);
    EXPECT_TRUE(a.max_size() == 0);
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(a.data() == nullptr);
  }
}

constexpr auto ShouldNotCallThis = "ShouldNotCall";

TEST_VM_FATAL_ERROR_MSG(ZCArray, empty_front, ShouldNotCallThis) {
  using T = int;
  ZCArray<T, 0> a;
  static_cast<void>(a.front());
}

TEST_VM_FATAL_ERROR_MSG(ZCArray, const_empty_front, ShouldNotCallThis) {
  using T = int;
  const ZCArray<T, 0> a;
  static_cast<void>(a.front());
}

TEST_VM_FATAL_ERROR_MSG(ZCArray, empty_back, ShouldNotCallThis) {
  using T = int;
  ZCArray<T, 0> a;
  static_cast<void>(a.back());
}

TEST_VM_FATAL_ERROR_MSG(ZCArray, const_empty_back, ShouldNotCallThis) {
  using T = int;
  const ZCArray<T, 0> a;
  static_cast<void>(a.back());
}

TEST_VM_FATAL_ERROR_MSG(ZCArray, empty_operator_at, ShouldNotCallThis) {
  using T = int;
  ZCArray<T, 0> a;
  static_cast<void>(a[0]);
}

TEST_VM_FATAL_ERROR_MSG(ZCArray, const_empty_operator_at, ShouldNotCallThis) {
  using T = int;
  const ZCArray<T, 0> a;
  static_cast<void>(a[0]);
}

TEST_VM_FATAL_ERROR_MSG(ZCArray, empty_at, ShouldNotCallThis) {
  using T = int;
  ZCArray<T, 0> a;
  static_cast<void>(a.at(0));
}

TEST_VM_FATAL_ERROR_MSG(ZCArray, const_empty_at, ShouldNotCallThis) {
  using T = int;
  const ZCArray<T, 0> a;
  static_cast<void>(a.at(0));
}

TEST(ZCArray, construction) {
  using T = int;
  constexpr size_t size = 10;

  const auto check = [](auto& a) {
    EXPECT_TRUE(a.size() == size);
    EXPECT_TRUE(a.max_size() == size);
    EXPECT_TRUE(!a.empty());
    EXPECT_TRUE(a.front() == T());
    EXPECT_TRUE(a.back() == T());
    for (size_t i = 0; i < size; ++i) {
      EXPECT_TRUE(a[i] == T());
    }
  };

  {
    ZCArray<T, size> a = {{0}};
    check(a);
    const ZCArray<T, size> ca = {a};
    check(ca);
  }
  {
    const ZCArray<T, size> a = {{0}};
    check(a);
    const ZCArray<T, size> ca = {a};
    check(ca);
  }
  const auto check_with_first = [](auto& a, T&& first_value) {
    EXPECT_TRUE(a.size() == size);
    EXPECT_TRUE(a.max_size() == size);
    EXPECT_TRUE(!a.empty());
    EXPECT_TRUE(a[0] == first_value);
    EXPECT_TRUE(a.front() == first_value);
    EXPECT_TRUE(a.back() == T());
    for (size_t i = 1; i < size; ++i) {
      EXPECT_TRUE(a[i] == T());
    }
  };
  {
    ZCArray<T, size> a = {{1}};
    check_with_first(a, 1);
    const ZCArray<T, size> ca = {a};
    check_with_first(ca, 1);
  }
  {
    const ZCArray<T, size> a = {{1}};
    check_with_first(a, 1);
    const ZCArray<T, size> ca = {a};
    check_with_first(ca, 1);
  }
}

#ifdef ASSERT
TEST_VM_ASSERT(ZCArray, overflow_operator_at) {
  using T = int;
  constexpr size_t size = 1;
  ZCArray<T, size> a{};
  static_cast<void>(a[size]);
}

TEST_VM_ASSERT(ZCArray, overflow_const_operator_at) {
  using T = int;
  constexpr size_t size = 1;
  const ZCArray<T, size> a{};
  static_cast<void>(a[size]);
}
#endif

constexpr auto guarantee = "guarantee";

TEST_VM_FATAL_ERROR_MSG(ZCArray, non_empty_at, guarantee) {
  using T = int;
  constexpr size_t size = 1;
  ZCArray<T, size> a{};
  static_cast<void>(a.at(size));
}

TEST_VM_FATAL_ERROR_MSG(ZCArray, const_non_empty_at, guarantee) {
  using T = int;
  constexpr size_t size = 1;
  const ZCArray<T, size> a{};
  static_cast<void>(a.at(size));
}

TEST(ZCArray, fill) {
  using T = int;
  constexpr size_t size = 10;

  const auto check = [](auto& a, T&& check_value) {
    EXPECT_TRUE(a.size() == size);
    EXPECT_TRUE(a.max_size() == size);
    EXPECT_TRUE(!a.empty());
    for (size_t i = 0; i < size; ++i) {
      EXPECT_TRUE(a[i] == check_value);
    }
  };
  {
    ZCArray<T, size> a = {{0}};
    check(a, 0);
    const ZCArray<T, size> a0 = {a};
    check(a0, 0);
    a.fill(1);
    check(a, 1);
    const ZCArray<T, size> a1 = {a};
    check(a1, 1);
  }
}

TEST(ZCArray, operator_cmp) {
  using T = int;
  const ZCArray<T, 2> a00{{0,0}};
  const ZCArray<T, 2> a01{{0,1}};
  const ZCArray<T, 2> a10{{1,0}};
  const ZCArray<T, 2> a11{{1,1}};

#define EXPECT_OP(lhs, op, A00, A01, A10, A11) \
  EXPECT_##A00(lhs op a00); EXPECT_##A01(lhs op a01); EXPECT_##A10(lhs op a10); EXPECT_##A11(lhs op a11)

  //        lhs  ==    a00,   a01,   a10,   a11
  EXPECT_OP(a00, ==,  TRUE, FALSE, FALSE, FALSE);
  EXPECT_OP(a01, ==, FALSE,  TRUE, FALSE, FALSE);
  EXPECT_OP(a10, ==, FALSE, FALSE,  TRUE, FALSE);
  EXPECT_OP(a11, ==, FALSE, FALSE, FALSE,  TRUE);

  //        lhs  !=    a00,   a01,   a10,   a11
  EXPECT_OP(a00, !=, FALSE,  TRUE,  TRUE,  TRUE);
  EXPECT_OP(a01, !=,  TRUE, FALSE,  TRUE,  TRUE);
  EXPECT_OP(a10, !=,  TRUE,  TRUE, FALSE,  TRUE);
  EXPECT_OP(a11, !=,  TRUE,  TRUE,  TRUE, FALSE);

  //        lhs   <    a00,   a01,   a10,   a11
  EXPECT_OP(a00,  <, FALSE,  TRUE,  TRUE,  TRUE);
  EXPECT_OP(a01,  <, FALSE, FALSE,  TRUE,  TRUE);
  EXPECT_OP(a10,  <, FALSE, FALSE, FALSE,  TRUE);
  EXPECT_OP(a11,  <, FALSE, FALSE, FALSE, FALSE);

  //        lhs  <=    a00,   a01,   a10,   a11
  EXPECT_OP(a00, <=,  TRUE,  TRUE,  TRUE,  TRUE);
  EXPECT_OP(a01, <=, FALSE,  TRUE,  TRUE,  TRUE);
  EXPECT_OP(a10, <=, FALSE, FALSE,  TRUE,  TRUE);
  EXPECT_OP(a11, <=, FALSE, FALSE, FALSE,  TRUE);

  //        lhs   >    a00,   a01,   a10,   a11
  EXPECT_OP(a00,  >, FALSE, FALSE, FALSE, FALSE);
  EXPECT_OP(a01,  >,  TRUE, FALSE, FALSE, FALSE);
  EXPECT_OP(a10,  >,  TRUE,  TRUE, FALSE, FALSE);
  EXPECT_OP(a11,  >,  TRUE,  TRUE,  TRUE, FALSE);

  //        lhs  >=    a00,   a01,   a10,   a11
  EXPECT_OP(a00, >=,  TRUE, FALSE, FALSE, FALSE);
  EXPECT_OP(a01, >=,  TRUE,  TRUE, FALSE, FALSE);
  EXPECT_OP(a10, >=,  TRUE,  TRUE,  TRUE, FALSE);
  EXPECT_OP(a11, >=,  TRUE,  TRUE,  TRUE,  TRUE);

#undef EXPECT_OP
}

#endif // PRODUCT
