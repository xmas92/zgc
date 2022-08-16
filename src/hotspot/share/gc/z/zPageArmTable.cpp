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

#include "precompiled.hpp"
#include "gc/z/zGlobals.hpp"
#include "gc/z/zPage.inline.hpp"
#include "gc/z/zPageArmTable.inline.hpp"
#include "logging/log.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/os.hpp"

int8_t ZPageArmGoodMask;
int8_t ZPageArmBadMask;

ZGenerationId* ZPageArmTable::_table = NULL;
intptr_t ZPageArmTable::_table_bias = 0;

// Note that this table is placed in a low-address location,
// so that it can be encoded in the cmp instruction. This is
// faster than issuing an explicit load of the table address.
// See the usage of table_bias in cmp_page_bits.

bool ZPageArmTable::reserve() {
  // table_bias = table_base - addr_base >> ZGranuleSizeShift
  // table_bias = table_base - addr_shifted_base
  // table_base = addr_shifted_base + table_bias
  size_t table_size = 2ULL * 1024 * 1024;
  uintptr_t search_start = 1800ULL * 1024 * 1024;
  char* addr_end = ((char*)search_start) + table_size * 128;
  for (char* addr = (char*)search_start; addr != addr_end; addr += table_size) {
    char* addr_bias = (char*)table_bias((uintptr_t)addr);
    log_debug(gc)("Page type table addr: " PTR_FORMAT " bias addr: " PTR_FORMAT " shifted addr_base: " PTR_FORMAT,
                  p2i(addr), p2i(addr_bias), ZAddressHeapBase >> ZGranuleSizeShift);
    if (os::attempt_reserve_memory_at(addr, table_size)) {
      if (os::attempt_reserve_memory_at(addr_bias, os::vm_page_size())) {
        os::commit_memory_or_exit(addr, table_size, false, "Heap initialization error due to out of memory");
        os::commit_memory_or_exit(addr_bias, os::vm_page_size(), false, "Heap initialization error due to out of memory");
        memset(addr, (int)ZGenerationId::old, table_size);
        *addr_bias = 0;
        _table = reinterpret_cast<ZGenerationId*>(addr);
        _table_bias = reinterpret_cast<uintptr_t>(addr_bias);
        return true;
      } else {
        os::release_memory(addr, table_size);
      }
    }
  }

  return false;
}

void ZPageArmTable::initialize() {
  if (!reserve()) {
    // Can't use the arm table
    FLAG_SET_ERGO(ZArmPages, false);
    return;
  }

  ZPageArmGoodMask = 4;
  ZPageArmBadMask = ZPageArmGoodMask ^ ZPageArmMetadataMask;
}

void ZPageArmTable::flip_young_mark_start() {
  ZPageArmGoodMask ^= ZPageArmYoungMetadataMask;
}

intptr_t ZPageArmTable::table_bias(uintptr_t table_base) {
  uintptr_t address_base = ZAddressHeapBase;
  assert(table_base > address_base >> ZGranuleSizeShift, "negative table bias");
  return intptr_t(table_base) - (address_base >> ZGranuleSizeShift);
}

void ZPageArmTable::set(zaddress addr, int8_t arm_value) {
  uintptr_t value = untype(addr);
  Atomic::store(reinterpret_cast<int8_t*>((value >> addr_shift()) + table_bias()), arm_value);
}

void ZPageArmTable::set(ZPage* page, int8_t arm_value) {
  for (zoffset_end i = (zoffset_end)page->start(); i < page->end(); i += ZGranuleSize) {
    set(ZOffset::address((zoffset)i), arm_value);
  }
}
