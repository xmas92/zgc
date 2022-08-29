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

#include "gc/z/zAddress.hpp"
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

static uint8_t countl_one(uint8_t v) {
  return __builtin_clz(~v);
}

static uint64_t rotr(uint64_t x, uint8_t r) {
  return (x >> r) | (x << (64 - r));
}
static uint64_t decode(uint8_t size, uint8_t shift, uint8_t rotation) {
    uint64_t result = ~0ull;
    result >>= (64 - shift);
    for (uint8_t e = size; e < 64; e *= 2) {
        result |= (result << e);
    }
    result = rotr(result, rotation);
    return result;
}
// sf opc      N immr   imms   Rn    Rd
// 1  11100100 1 000000 000000 00000 11111
// 1  11100100 1 111111 111111 00000 11111
static uint64_t decode(bool n, uint8_t immr, uint8_t imms) {
    const uint8_t size = n ? 64 : (1 << (5 - countl_one(imms)));
    const uint8_t shift = (imms & (size-1)) + 1;
    const uint8_t rotation = immr;
    return decode(size, shift, rotation);
}
static uint64_t decode(uint16_t n_immr_imms) {
  assert((n_immr_imms & ~0x1fff) == 0, "bad immediate");
  bool n = (n_immr_imms >> 12) & 0b1;
  uint8_t immr = (n_immr_imms >> 6) & 0b111111;
  uint8_t imms = n_immr_imms & 0b111111;
  return decode(n, immr, imms);
}
bool ZPageArmTable::try_reserve(char* addr) {
  size_t table_size = 2ULL * 1024 * 1024;
  char* addr_bias = (char*)table_bias((uintptr_t)addr);
  log_debug(gc)("Page type table addr: " PTR_FORMAT " bias addr: " PTR_FORMAT " shifted addr_base: " PTR_FORMAT
                " ZAddressHeapBase: " PTR_FORMAT,
                p2i(addr), p2i(addr_bias), ZAddressHeapBase >> ZGranuleSizeShift, ZAddressHeapBase);
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

  return false;
}

bool ZPageArmTable::reserve_bsd_aarch64() {
  size_t table_size = 2ULL * 1024 * 1024;
  for (uint8_t size = 2; size <= 64; size *= 2) {
    for (uint8_t shift = 1; shift < size; ++shift) {
      for (uint8_t rotation = 0; rotation < size; ++rotation) {
        uint64_t addr_bias = decode(size, shift, rotation);
        if (addr_bias > ZAddressHeapBase || (addr_bias & ((1 << ZGranuleSizeShift) - 1))) {
          continue;
        }
        char* addr = (char*)((ZAddressHeapBase >> ZGranuleSizeShift) + addr_bias);
        if (addr_bias >= (ZAddressHeapBase - table_size)) {
          continue;
        }
        if (try_reserve(addr)) {
          return true;
        }
      }
    }
  }
  return false;
}

// Note that this table is placed in a low-address location,
// so that it can be encoded in the cmp instruction. This is
// faster than issuing an explicit load of the table address.

bool ZPageArmTable::reserve() {
  // table_bias = table_base - addr_base >> ZGranuleSizeShift
  // table_bias = table_base - addr_shifted_base
  // table_base = addr_shifted_base + table_bias
  return reserve_bsd_aarch64();
  size_t table_size = 2ULL * 1024 * 1024;
  uintptr_t search_start = (1ull << 34);
  char* addr_end = ((char*)search_start) + table_size * 128;
  for (char* addr = (char*)search_start; addr != addr_end; addr += table_size) {
    if (try_reserve(addr)) {
      return true;
    }
  }
  // 0x0000000000000010
  return false;
}

void ZPageArmTable::initialize() {
  if (!reserve()) {
    // Can't use the arm table
    FLAG_SET_ERGO(ZArmPages, false);
    return;
  }

  ZPageArmGoodMask = ZPageArmYoung0;
  ZPageArmBadMask = ZPageArmGoodMask ^ ZPageArmMetadataMask;
  set(zaddress::null, ZPageArmGoodMask);
}

void ZPageArmTable::flip_young_mark_start() {
  ZPageArmGoodMask ^= ZPageArmYoungMetadataMask;
  ZPageArmBadMask = ZPageArmGoodMask ^ ZPageArmMetadataMask;
  set(zaddress::null, ZPageArmGoodMask);
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
