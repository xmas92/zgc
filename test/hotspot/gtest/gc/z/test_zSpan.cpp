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
#include "gc/z/zSpan.hpp"
#include "metaprogramming/isSame.hpp"
#include "unittest.hpp"

#ifndef PRODUCT

 // https://en.cppreference.com/w/cpp/container/span/span

 // TODO(axel): Instrument gtest with compilation error checking
 //             and extend the tests to check the API

TEST(ZSpan, constructor_1) {
  using T = int;
  {
    const ZSpan<T> s;
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == nullptr);
  }
  {
    const ZSpan<T> s{};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == nullptr);
  }
  {
    const ZSpan<T, 0> s;
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == nullptr);
  }
  {
    const ZSpan<T, 0> s{};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == nullptr);
  }
}

TEST(ZSpan, constructor_2) {
  using T = int;

  {
    const ZSpan<T> s{nullptr, (size_t)0};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == nullptr);
  }
  {
    const ZSpan<T,0> s{nullptr, (size_t)0};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == nullptr);
  }

  constexpr size_t count = 2;
  T data[count] = {0};

  {
    const ZSpan<T> s{data, (size_t)0};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.data() == data);
  }
  {
    const ZSpan<T, 0> s{data, (size_t)0};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.data() == data);
  }
  {
    const ZSpan<T> s{data, count};
    EXPECT_TRUE(s.size() == count);
    EXPECT_TRUE(s.data() == data);
  }
  {
    const ZSpan<T, count> s{data, count};
    EXPECT_TRUE(s.size() == count);
    EXPECT_TRUE(s.data() == data);
  }
}
TEST_VM_ASSERT(ZSpan, constructor_2_wrong_count) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T, 1> s{data, count};
}
TEST_VM_ASSERT(ZSpan, constructor_2_nullptr) {
  using T = int;
  constexpr size_t count = 2;
  const ZSpan<T, 2> s{nullptr, count};
}
TEST_VM_ASSERT(ZSpan, constructor_2_nullptr_dynamic) {
  using T = int;
  constexpr size_t count = 2;
  const ZSpan<T> s{nullptr, count};
}

TEST(ZSpan, constructor_3) {
  using T = int;

  {
    const ZSpan<T> s{nullptr, nullptr};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == nullptr);
  }
  {
    const ZSpan<T,0> s{nullptr, nullptr};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == nullptr);
  }

  constexpr size_t count = 4;
  constexpr size_t middle = 2;
  T data[count] = {0};

  {
    const ZSpan<T> s{data, data};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == data);
  }
  {
    const ZSpan<T, 0> s{data, data};
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.data() == data);
  }
  {
    const ZSpan<T> s{data, data + middle};
    EXPECT_TRUE(s.size() == middle);
    EXPECT_TRUE(s.data() == data);
    for (size_t i = 0; i < middle; ++i) {
      EXPECT_TRUE(&s[i] == &data[i]);
    }
  }
  {
    const ZSpan<T, middle> s{data, data + middle};
    EXPECT_TRUE(s.size() == middle);
    EXPECT_TRUE(s.data() == data);
    for (size_t i = 0; i < middle; ++i) {
      EXPECT_TRUE(&s[i] == &data[i]);
    }
  }
  {
    const ZSpan<T> s{data + middle, data + count};
    EXPECT_TRUE(s.size() == (count - middle));
    EXPECT_TRUE(s.data() == data + middle);
    for (size_t i = 0; i < middle; ++i) {
      EXPECT_TRUE(&s[i] == &data[middle + i]);
    }
  }
  {
    const ZSpan<T, (count - middle)> s{data + middle, data + count};
    EXPECT_TRUE(s.size() == (count - middle));
    EXPECT_TRUE(s.data() == data + middle);
    for (size_t i = 0; i < middle; ++i) {
      EXPECT_TRUE(&s[i] == &data[middle + i]);
    }
  }
}
TEST_VM_ASSERT(ZSpan, constructor_3_wrong_count) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T, 1> s{data, data + count};
}

TEST(ZSpan, constructor_4) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};

  {
      const ZSpan<T> s{data};
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data);
  }
  {
      const ZSpan<T, count> s{data};
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data);
  }
  {
      const ZSpan<T> s = data;
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data);
  }
  {
      const ZSpan<T, count> s = data;
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data);
  }
}

TEST(ZSpan, constructor_5) {
  using T = int;
  constexpr size_t count = 2;
  ZCArray<T, count> data = {0, 0};

  {
      const ZSpan<T> s{data};
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
  {
      const ZSpan<T, count> s{data};
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
  {
      const ZSpan<T> s = data;
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
  {
      const ZSpan<T, count> s = data;
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
  {
      const ZSpan<const T> s = data;
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
  {
      const ZSpan<const T, count> s = data;
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
}

TEST(ZSpan, constructor_6) {
  using T = int;
  constexpr size_t count = 2;
  const ZCArray<T, count> data = {0, 0};

  {
      const ZSpan<const T> s{data};
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
  {
      const ZSpan<const T, count> s{data};
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
  {
      const ZSpan<const T> s = data;
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
  {
      const ZSpan<const T, count> s = data;
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data.data());
  }
}

TEST(ZSpan, constructor_8) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> span{data};

  const ZSpan<T, count> s{span};
  EXPECT_TRUE(s.size() == count);
  EXPECT_TRUE(s.data() == data);
}
TEST_VM_ASSERT(ZSpan, constructor_8_wrong_count) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> span{data};

  const ZSpan<T, 1> s{span};
}

TEST(ZSpan, constructor_9) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> span{data};

  const ZSpan<T> s{span};
  EXPECT_TRUE(s.size() == count);
  EXPECT_TRUE(s.data() == data);
}

// https://en.cppreference.com/w/cpp/container/span/operator%3D

TEST(ZSpan, copy_operator) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};

  const auto get_span = [&]() -> ZSpan<T> { return {data, count}; };
  const auto use_span = [&](ZSpan<const T> s) {
      EXPECT_TRUE(s.size() == count);
      EXPECT_TRUE(s.data() == data);
  };

  ZSpan<T> s1;
  EXPECT_TRUE(s1.empty());

  ZSpan<const T> s2 = data;
  EXPECT_TRUE(s2.size() == count);
  EXPECT_TRUE(s2.data() == data);

  s2 = s1;
  EXPECT_TRUE(s2.empty());
  use_span(get_span());

  s1 = get_span();
  EXPECT_TRUE(s1.size() == count);
  EXPECT_TRUE(s1.data() == data);
}

// https://en.cppreference.com/w/cpp/container/span/front

TEST(ZSpan, front) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  {
    const ZSpan<T> s = data;
    EXPECT_TRUE(&s.front() == &data[0]);
  }
  {
    const ZSpan<T, count> s = data;
    EXPECT_TRUE(&s.front() == &data[0]);
  }
}
TEST_VM_ASSERT(ZSpan, front) {
  using T = int;
  const ZSpan<T> s;
  s.front();
}

// https://en.cppreference.com/w/cpp/container/span/back

TEST(ZSpan, back) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  {
    const ZSpan<T> s = data;
    EXPECT_TRUE(&s.back() == &data[count-1]);
  }
  {
    const ZSpan<T, count> s = data;
    EXPECT_TRUE(&s.back() == &data[count-1]);
  }
}
TEST_VM_ASSERT(ZSpan, back) {
  using T = int;
  const ZSpan<T> s;
  s.back();
}

// https://en.cppreference.com/w/cpp/container/span/operator_at

TEST(ZSpan, operator_at) {
  using T = int;
  constexpr size_t count = 10;
  T data[count] = {0};
  const auto clear = [](const ZSpan<T> s) {
    for (size_t i = 0; i < s.size(); ++i) {
      s[i] = 0;
    }
  };
  const auto check = [](const ZSpan<T> s) -> bool {
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] != 0) {
        return false;
      }
    }
    return true;
  };
  {
    const ZSpan<T> s = data;
    for (size_t i = 0; i < count; ++i) {
      const auto val = static_cast<T>(i);
      EXPECT_TRUE(&s[i] == &data[i]);
      s[i] = val;
      EXPECT_TRUE(s[i] == val);
      EXPECT_TRUE(val == data[i]);
    }
  }

  EXPECT_FALSE(check(data));
  clear(data);
  EXPECT_TRUE(check(data));
}

// https://en.cppreference.com/w/cpp/container/span/first

TEST(ZSpan, first) {
  using T = int;
  constexpr size_t count = 10;
  constexpr size_t half = count / 2;
  T data[count] = {0};

  {
    const ZSpan<T> s;
    EXPECT_TRUE(s.first<0>().size() == 0);
    EXPECT_TRUE(s.first(0).size() == 0);
  }
  {
    const ZSpan<T, 0> s;
    EXPECT_TRUE(s.first<0>().size() == 0);
    EXPECT_TRUE(s.first(0).size() == 0);
  }
  {
    const ZSpan<T> s = data;
    EXPECT_TRUE(s.first<0>().size() == 0);
    EXPECT_TRUE(s.first(0).size() == 0);
  }
  {
    const ZSpan<T, count> s = data;
    EXPECT_TRUE(s.first<0>().size() == 0);
    EXPECT_TRUE(s.first(0).size() == 0);
  }
  {
    const ZSpan<T> s = data;
    EXPECT_TRUE(s.first<half>().size() == half);
    EXPECT_TRUE(s.first(half).size() == half);
  }
  {
    const ZSpan<T, count> s = data;
    EXPECT_TRUE(s.first<half>().size() == half);
    EXPECT_TRUE(s.first(half).size() == half);
  }
  {
    const ZSpan<T> s = data;
    EXPECT_TRUE(s.first<count>().size() == count);
    EXPECT_TRUE(s.first(count).size() == count);
  }
  {
    const ZSpan<T, count> s = data;
    EXPECT_TRUE(s.first<count>().size() == count);
    EXPECT_TRUE(s.first(count).size() == count);
  }
  {
    const ZSpan<T, count> span = data;
    const auto s1 = span.first<count>();
    for (size_t i = 0; i < s1.size(); ++i) {
      EXPECT_TRUE(&s1[i] == &data[i]);
    }
    const auto s2 = s1.first<half>();
    for (size_t i = 0; i < s2.size(); ++i) {
      EXPECT_TRUE(&s2[i] == &data[i]);
    }
  }
  {
    const ZSpan<T> span = data;
    const auto s1 = span.first(count);
    for (size_t i = 0; i < s1.size(); ++i) {
      EXPECT_TRUE(&s1[i] == &data[i]);
    }
    const auto s2 = s1.first(half);
    for (size_t i = 0; i < s2.size(); ++i) {
      EXPECT_TRUE(&s2[i] == &data[i]);
    }
  }
}
TEST_VM_ASSERT(ZSpan, first_overflow) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T, count> s{data};
  s.first(count + 1);
}
TEST_VM_ASSERT(ZSpan, first_overflow_dynamic1) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> s{data};
  s.first(count + 1);
}
TEST_VM_ASSERT(ZSpan, first_overflow_dynamic2) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> s{data};
  s.first<count + 1>();
}

// https://en.cppreference.com/w/cpp/container/span/last

TEST(ZSpan, last) {
  using T = int;
  constexpr size_t count = 10;
  constexpr size_t half = count / 2;
  T data[count] = {0};

  {
    const ZSpan<T> s;
    EXPECT_TRUE(s.last<0>().size() == 0);
    EXPECT_TRUE(s.last(0).size() == 0);
  }
  {
    const ZSpan<T, 0> s;
    EXPECT_TRUE(s.last<0>().size() == 0);
    EXPECT_TRUE(s.last(0).size() == 0);
  }
  {
    const ZSpan<T> s = data;
    EXPECT_TRUE(s.last<0>().size() == 0);
    EXPECT_TRUE(s.last(0).size() == 0);
  }
  {
    const ZSpan<T, count> s = data;
    EXPECT_TRUE(s.last<0>().size() == 0);
    EXPECT_TRUE(s.last(0).size() == 0);
  }
  {
    const ZSpan<T> s = data;
    EXPECT_TRUE(s.last<half>().size() == half);
    EXPECT_TRUE(s.last(half).size() == half);
  }
  {
    const ZSpan<T, count> s = data;
    EXPECT_TRUE(s.last<half>().size() == half);
    EXPECT_TRUE(s.last(half).size() == half);
  }
  {
    const ZSpan<T> s = data;
    EXPECT_TRUE(s.last<count>().size() == count);
    EXPECT_TRUE(s.last(count).size() == count);
  }
  {
    const ZSpan<T, count> s = data;
    EXPECT_TRUE(s.last<count>().size() == count);
    EXPECT_TRUE(s.last(count).size() == count);
  }
  {
    const ZSpan<T, count> span = data;
    const auto s1 = span.last<count>();
    for (size_t i = 0; i < s1.size(); ++i) {
      EXPECT_TRUE(&s1[i] == &data[i]);
    }
    const auto s2 = s1.last<half>();
    for (size_t i = 0; i < s2.size(); ++i) {
      EXPECT_TRUE(&s2[i] == &data[half + i]);
    }
  }
  {
    const ZSpan<T> span = data;
    const auto s1 = span.last(count);
    for (size_t i = 0; i < s1.size(); ++i) {
      EXPECT_TRUE(&s1[i] == &data[i]);
    }
    const auto s2 = s1.last(half);
    for (size_t i = 0; i < s2.size(); ++i) {
      EXPECT_TRUE(&s2[i] == &data[half + i]);
    }
  }
}
TEST_VM_ASSERT(ZSpan, last_overflow) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T, count> s{data};
  s.last(count + 1);
}
TEST_VM_ASSERT(ZSpan, last_overflow_dynamic1) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> s{data};
  s.last(count + 1);
}
TEST_VM_ASSERT(ZSpan, last_overflow_dynamic2) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> s{data};
  s.last<count + 1>();
}

// https://en.cppreference.com/w/cpp/container/span/subspan

TEST(ZSpan, subspan) {
  using T = int;
  constexpr size_t count = 20;
  constexpr size_t half = count / 2;
  constexpr size_t quarter = half / 2;
  T data[count] = {0};

  {
    const ZSpan<T, count> s = data;
    const ZSpan<T, half> ss = s.subspan<quarter,half>();

    EXPECT_TRUE(ss.size() == half);
    EXPECT_TRUE(ss.data() == &data[quarter]);

    EXPECT_TRUE((ss.subspan<0, half>()).size() == half);
    EXPECT_TRUE(ss.subspan(0, quarter).size() == quarter);

    EXPECT_TRUE((ss.subspan<quarter, quarter>()).data() == &data[half]);
  }

  {
    const ZSpan<T> s = data;
    const ZSpan<T, half> ss = s.subspan<quarter,half>();

    EXPECT_TRUE(ss.size() == half);
    EXPECT_TRUE(ss.data() == &data[quarter]);

    EXPECT_TRUE((ss.subspan<0, half>()).size() == half);
    EXPECT_TRUE(ss.subspan(0, quarter).size() == quarter);

    EXPECT_TRUE((ss.subspan<quarter, quarter>()).data() == &data[half]);
  }

  {
    const ZSpan<T, count> s = data;
    const ZSpan<T, 0> ss = s.subspan<quarter,0>();

    EXPECT_TRUE(ss.size() == 0);
    EXPECT_TRUE(ss.data() == &data[quarter]);
  }

  {
    const ZSpan<T, count> s = data;
    const ZSpan<T, count-quarter> ss = s.subspan<quarter>();

    EXPECT_TRUE(ss.size() == count-quarter);
    EXPECT_TRUE(ss.data() == &data[quarter]);
  }

  {
    const ZSpan<T, count> s = data;
    const ZSpan<T> ss = s.subspan(quarter);

    EXPECT_TRUE(ss.size() == count-quarter);
    EXPECT_TRUE(ss.data() == &data[quarter]);
  }

  {
    const ZSpan<T> s;
    const ZSpan<T, 0> ss = s.subspan<0,0>();

    EXPECT_TRUE((s.subspan<0,0>()).size() == 0);
    EXPECT_TRUE((s.subspan<0,0>()).data() == nullptr);

    EXPECT_TRUE((s.subspan<0>()).size() == 0);
    EXPECT_TRUE((s.subspan<0>()).data() == nullptr);

    EXPECT_TRUE(s.subspan(0,0).size() == 0);
    EXPECT_TRUE(s.subspan(0,0).data() == nullptr);

    EXPECT_TRUE(s.subspan(0).size() == 0);
    EXPECT_TRUE(s.subspan(0).data() == nullptr);
  }
}
TEST_VM_ASSERT(ZSpan, subspan_offset_overflow) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T, count> s{data};
  s.subspan(count + 1);
}
TEST_VM_ASSERT(ZSpan, subspan_offset_dynamic1) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> s{data};
  s.subspan(count + 1);
}
TEST_VM_ASSERT(ZSpan, subspan_offset_dynamic2) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> s{data};
  s.subspan<count + 1>();
}
TEST_VM_ASSERT(ZSpan, subspan_count_overflow) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T, count> s{data};
  s.subspan(1, count);
}
TEST_VM_ASSERT(ZSpan, subspan_count_dynamic1) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> s{data};
  s.subspan(1, count);
}
TEST_VM_ASSERT(ZSpan, subspan_count_dynamic2) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> s{data};
  s.subspan<1, count>();
}

// https://en.cppreference.com/w/cpp/container/span/as_bytes

TEST(ZSpan, as_bytes) {
  using T = int;
  {
    const ZSpan<T> s;
    const auto bs = as_bytes(s);

    EXPECT_TRUE(bs.size() == s.size_bytes());
    EXPECT_TRUE(bs.size() == 0);
    EXPECT_TRUE(bs.data() == nullptr);
    EXPECT_TRUE(bs.data() == static_cast<const void*>(s.data()));
  }
  {
    const ZSpan<T, 0> s;
    const ZSpan<const zbyte, 0> bs = as_bytes(s);

    EXPECT_TRUE(bs.size() == s.size_bytes());
    EXPECT_TRUE(bs.size() == 0);
    EXPECT_TRUE(bs.data() == nullptr);
    EXPECT_TRUE(bs.data() == static_cast<const void*>(s.data()));
  }

  constexpr size_t count = 2;
  T data[count] = {0};

  {
    const ZSpan<T> s = data;
    const auto bs = as_bytes(s);

    EXPECT_TRUE(bs.size() == s.size_bytes());
    EXPECT_TRUE(bs.data() == static_cast<const void*>(s.data()));
  }
  {
    const ZSpan<T, count> s = data;
    const  ZSpan<const zbyte, sizeof(T) * count> bs = as_bytes(s);

    EXPECT_TRUE(bs.size() == s.size_bytes());
    EXPECT_TRUE(bs.data() == static_cast<const void*>(s.data()));
  }
}

TEST(ZSpan, as_writable_bytes) {
  using T = int;
  {
    const ZSpan<T> s;
    const auto bs = as_writable_bytes(s);

    EXPECT_TRUE(bs.size() == s.size_bytes());
    EXPECT_TRUE(bs.size() == 0);
    EXPECT_TRUE(bs.data() == nullptr);
    EXPECT_TRUE(bs.data() == static_cast<const void*>(s.data()));
  }
  {
    const ZSpan<T, 0> s;
    const ZSpan<zbyte, 0> bs = as_writable_bytes(s);

    EXPECT_TRUE(bs.size() == s.size_bytes());
    EXPECT_TRUE(bs.size() == 0);
    EXPECT_TRUE(bs.data() == nullptr);
    EXPECT_TRUE(bs.data() == static_cast<const void*>(s.data()));
  }

  constexpr size_t count = 2;
  T data[count] = {0};

  {
    const ZSpan<T> s = data;
    const auto bs = as_writable_bytes(s);

    EXPECT_TRUE(bs.size() == s.size_bytes());
    EXPECT_TRUE(bs.data() == static_cast<const void*>(s.data()));
  }
  {
    const ZSpan<T, count> s = data;
    const  ZSpan<zbyte, sizeof(T) * count> bs = as_writable_bytes(s);

    EXPECT_TRUE(bs.size() == s.size_bytes());
    EXPECT_TRUE(bs.data() == static_cast<const void*>(s.data()));
  }
}

TEST(ZSpan, sizeof) {
  using T = int;
  {
    const ZSpan<T> s;
    EXPECT_TRUE(sizeof(s) == sizeof(ZSpan<T>::pointer) + sizeof(ZSpan<T>::difference_type));
  }
  {
    const ZSpan<T, 0> s;
    EXPECT_TRUE(sizeof(s) == sizeof(ZSpan<T>::pointer));
  }
}

#endif // PRODUCT
