/*
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_GC_Z_ZPAGEARMTABLE_INLINE_HPP
#define SHARE_GC_Z_ZPAGEARMTABLE_INLINE_HPP

#include "gc/z/zAddress.inline.hpp"
#include "gc/z/zPageArmTable.hpp"
#include "gc/z/zGlobals.hpp"
#include "runtime/atomic.hpp"

inline intptr_t ZPageArmTable::table_bias() {
  return _table_bias;
}

inline int ZPageArmTable::addr_shift() {
  // addr / page_size * table_element_size
  return ZGranuleSizeShift;
}

inline int8_t ZPageArmTable::get(zaddress addr) {
  uintptr_t value = untype(addr);
  return Atomic::load(reinterpret_cast<int8_t*>((value >> addr_shift()) + table_bias()));
}

inline int8_t ZPageArmTable::get(volatile zpointer* p) {
  return get(to_zaddress((uintptr_t)p));
}

#endif // SHARE_GC_Z_ZPAGEARMTABLE_INLINE_HPP
