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
#include "utilities/debug.hpp"
#include "utilities/macros.hpp"

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
#ifdef ASSERT
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
#endif

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
#ifdef ASSERT
TEST_VM_ASSERT(ZSpan, constructor_3_wrong_count) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T, 1> s{data, data + count};
}
#endif

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
#ifdef ASSERT
TEST_VM_ASSERT(ZSpan, constructor_8_wrong_count) {
  using T = int;
  constexpr size_t count = 2;
  T data[count] = {0};
  const ZSpan<T> span{data};

  const ZSpan<T, 1> s{span};
}
#endif

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
#ifdef ASSERT
TEST_VM_ASSERT(ZSpan, front) {
  using T = int;
  const ZSpan<T> s;
  s.front();
}
#endif

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
#ifdef ASSERT
TEST_VM_ASSERT(ZSpan, back) {
  using T = int;
  const ZSpan<T> s;
  s.back();
}
#endif

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
#ifdef ASSERT
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
#endif

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
#ifdef ASSERT
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
#endif

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
#ifdef ASSERT
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
#endif

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
  {
    const ZSpan<T> s;
    auto it = s.begin();
    EXPECT_TRUE(sizeof(it) == DEBUG_ONLY(3 * ) sizeof(ZSpan<T>::pointer));
  }
  {
    const ZSpan<T, 0> s;
    auto it = s.begin();
    EXPECT_TRUE(sizeof(it) == DEBUG_ONLY(3 * ) sizeof(ZSpan<T>::pointer));
  }
}

template<typename Span, size_t Size>
static void iterator_test() {
  using T = typename Span::value_type;
  constexpr size_t size = Size;
  STATIC_ASSERT(Span::extent == dynamic_extent || Span::extent == Size);
  STATIC_ASSERT(size % 2 == 0);
  ZCArray<T, size> a{};
  {
    Span s = a;
    T value{};
    for (T& v : s) {
      v = value++;
    }
  }
  {
    Span s = a;
    T value{};
    for (const T& v : s) {
      EXPECT_TRUE(v == value++);
    }
  }
  {
    const Span s = a;
    const auto cb = s.begin();
    const auto ce = s.end();
    T i{};
    for (const T& v : a) {
      EXPECT_TRUE(cb[i] == v);
      EXPECT_TRUE(ce[-(size - i++)] == v);
    }
  }
  {
    Span s = a;
    T value{};
    for (auto it = s.begin(), end = s.end(); it != end; ++it) {
      *it = 0;
    }
  }
  {
    const Span s = a;
    for (auto it = s.begin(), end = s.end(); it != end; ++it) {
      EXPECT_TRUE(*it == 0);
    }
  }
  {
    Span s = a;
    auto b = s.begin(), e = s.end();
    b += size / 2;
    e -= size / 2;
    EXPECT_TRUE(b == e);
    b += size / 2;
    e -= size / 2;
    EXPECT_TRUE(s.begin() == e);
    EXPECT_TRUE(b == s.end());
  }
  {
    const Span s = a;
    auto cb = s.begin(), ce = s.end();
    cb += size / 2;
    ce -= size / 2;
    EXPECT_TRUE(cb == ce);
    cb += size / 2;
    ce -= size / 2;
    EXPECT_TRUE(s.begin() == ce);
    EXPECT_TRUE(cb == s.end());
  }
  {
    const auto test = [&](auto b, auto e) {
      EXPECT_TRUE(b < e);
      EXPECT_TRUE(b <= e);
      EXPECT_FALSE(b > e);
      EXPECT_FALSE(b >= e);
      EXPECT_TRUE(b != e);
      EXPECT_FALSE(b == e);

      EXPECT_TRUE((b + (size / 2)) == (e - (size / 2)));
      EXPECT_TRUE((e - b) == size);
      EXPECT_TRUE((b - e) == -static_cast<ptrdiff_t>(size));
    };

    Span s = a;
    const Span cs = a;
    test(s.begin(), s.end());
    test(cs.begin(), cs.end());
  }
}

TEST(ZSpan, iterator) {
  using T = int;
  constexpr size_t size = 10;
  iterator_test<ZSpan<T>, size>();
  iterator_test<ZSpan<T, size>, size>();
  {
    ZCArray<T, 0> za;
    ZSpan<T> zsd = za;
    ZSpan<T, 0> zs0 = za;
    const auto test = [&](auto b, auto e) {
      EXPECT_FALSE(b < e);
      EXPECT_TRUE(b <= e);
      EXPECT_FALSE(b > e);
      EXPECT_TRUE(b >= e);
      EXPECT_FALSE(b != e);
      EXPECT_TRUE(b == e);

      EXPECT_TRUE(b == e);
      EXPECT_TRUE((e - b) == 0);
      EXPECT_TRUE((b - e) == 0);
    };

    test(zsd.begin(), zsd.end());
    test(zs0.begin(), zs0.end());
  }
}

template<typename SpanOne, typename SpanTwo, size_t Size>
static void test_different_iter() {
  using T = typename SpanOne::value_type;
  STATIC_ASSERT((IsSame<T, typename SpanTwo::value_type>::value));
  constexpr size_t size = Size;
  {
    ZCArray<T, size> a{};
    ZCArray<T, size> b{};
    SpanOne as = a;
    SpanTwo bs = b;
    static_cast<void>(as.begin() < bs.begin());
  }
}

#ifdef ASSERT
TEST_VM_ASSERT(ZSpan, different_iter) {
  using T = int;
  constexpr size_t size = 10;
  test_different_iter<ZSpan<T>, ZSpan<T>, size>();
}
TEST_VM_ASSERT(ZSpan, different_iter_fixed) {
  using T = int;
  constexpr size_t size = 10;
  test_different_iter<ZSpan<T, size>, ZSpan<T, size>, size>();
}
TEST_VM_ASSERT(ZSpan, different_iter_mix) {
  using T = int;
  constexpr size_t size = 10;
  test_different_iter<ZSpan<T>, ZSpan<T, size>, size>();
}

TEST_VM_ASSERT(ZSpan, underflow) {
  using T = int;
  constexpr size_t size = 10;
  ZCArray<T, size> a{};
  ZSpan<T> s = a;
  static_cast<void>(--s.begin());
}
TEST_VM_ASSERT(ZSpan, underflow_fixed) {
  using T = int;
  constexpr size_t size = 10;
  ZCArray<T, size> a{};
  ZSpan<T, size> s = a;
  static_cast<void>(--s.begin());
}

TEST_VM_ASSERT(ZSpan, overflow) {
  using T = int;
  constexpr size_t size = 10;
  ZCArray<T, size> a{};
  ZSpan<T> s = a;
  static_cast<void>(++s.end());
}
TEST_VM_ASSERT(ZSpan, overflow_fixed) {
  using T = int;
  constexpr size_t size = 10;
  ZCArray<T, size> a{};
  ZSpan<T, size> s = a;
  static_cast<void>(++s.end());
}

TEST_VM_ASSERT(ZSpan, underflow_at) {
  using T = int;
  constexpr size_t size = 10;
  ZCArray<T, size> a{};
  ZSpan<T> s = a;
  static_cast<void>(s.begin()[-1]);
}
TEST_VM_ASSERT(ZSpan, underflow_at_fixed) {
  using T = int;
  constexpr size_t size = 10;
  ZCArray<T, size> a{};
  ZSpan<T, size> s = a;
  static_cast<void>(s.begin()[-1]);
}

TEST_VM_ASSERT(ZSpan, overflow_at) {
  using T = int;
  constexpr size_t size = 10;
  ZCArray<T, size> a{};
  ZSpan<T> s = a;
  static_cast<void>(*s.end());
}
TEST_VM_ASSERT(ZSpan, overflow_at_fixed) {
  using T = int;
  constexpr size_t size = 10;
  ZCArray<T, size> a{};
  ZSpan<T, size> s = a;
  static_cast<void>(*s.end());
}
#endif // ASSERT
