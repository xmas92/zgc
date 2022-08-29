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

#ifndef SHARE_GC_Z_ZPAGEARMTABLE_HPP
#define SHARE_GC_Z_ZPAGEARMTABLE_HPP

#include "gc/z/zAddress.hpp"
#include "gc/z/zGenerationId.hpp"
#include "memory/allocation.hpp"

class ZPage;

constexpr int8_t ZPageArmOld = 1;
constexpr int8_t ZPageArmYoung0 = 2;
constexpr int8_t ZPageArmYoung1 = 4;
constexpr int8_t ZPageArmYoungMetadataMask = ZPageArmYoung0 | ZPageArmYoung1;
constexpr int8_t ZPageArmOldMetadataMask = ZPageArmOld;
constexpr int8_t ZPageArmMetadataMask = ZPageArmYoungMetadataMask | ZPageArmOldMetadataMask;
extern int8_t ZPageArmGoodMask;
extern int8_t ZPageArmBadMask;

class ZPageArmTable {
private:
  static ZGenerationId* _table;
  static intptr_t _table_bias;

  static intptr_t table_bias(uintptr_t table_base);
  static bool try_reserve(char* addr);
  static bool reserve_bsd_aarch64();
  static bool reserve();

public:
  static void initialize();
  static void flip_young_mark_start();

  static intptr_t table_bias();
  static int addr_shift();
  static int8_t get(zaddress value);
  static int8_t get(volatile zpointer* p);
  static void set(ZPage* page, int8_t arm_value);
  static void set(zaddress value, int8_t arm_value);
};

#endif // SHARE_GC_Z_ZPAGEARMTABLE_HPP
