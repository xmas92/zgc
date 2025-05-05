/*
 * Copyright (c) 2025, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

#ifndef SHARE_RUNTIME_ZSYNCHRONIZER_HPP
#define SHARE_RUNTIME_ZSYNCHRONIZER_HPP

#include "memory/allStatic.hpp"
#include "oops/oopsHierarchy.hpp"
#include "runtime/handles.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/sizes.hpp"

/*
  The main idea:

  Use both locking bits, GC cannot use markword marking.

  0b00 - Unlocked
  0b01 - Locked
  0b10 - Parking, Unlocked
  0b11 - Parking, Locked

  Enter:
    If Unlocked:
      try CAS 0bX0 -> 0bX1.
        Create thread local state.
        Done
    If Locked:
      Check thread local state for recursion.
        One inlined oop
        Walk thread local lock nodes
        Found:
          Increment recursion counter
        Else:
          Medium Enter
          Create thread local state.

  Medium Enter:
    Spin:
      try CAS 0bX0 -> 0bX1.
        Link thread local state.
        Done
      If Parking or N spin attempts:
        Slow Enter

  Slow Enter:
    If not parking:
      try CAS 0b01 -> 0b11.
      If Unlocked:
        Medium Enter (same spin state)
    Lock bucket.
      Link thread local state.
    Park.


  Exit:
    Check thread local state for recursion.
    If recursive:
      Decrement recursion counter
      Done
    Else:
      try CAS 0b01 -> 0b00.
        Remove thread local state.
        Done.
      Slow Exit
    Lock bucket.

  Slow Exit:
    Lock bucket.

  Wait:
    Slow Exit. (with wait state)

  Notify-N:
    Lock bucket.
    Switch N wait states.

*/

class BasicLock;
class JavaThread;
class LockZNode;
class LockZHashTable;

class LockZBasicLockState {
private:
  uintptr_t _data;

public:
  static constexpr uintptr_t MaxValue = alignof(LockZNode*);
  static constexpr uintptr_t ValueMask = MaxValue - 1;
  static constexpr uintptr_t RecursiveValue = 0;
  static constexpr uintptr_t PreLockedValue = 1;

  DEBUG_ONLY(static constexpr uintptr_t BadValue = ValueMask;)
  DEBUG_ONLY(static constexpr LockZBasicLockState bad_state() { return { BadValue }; })

  static constexpr LockZBasicLockState recursive_state() { return { RecursiveValue }; }

  // STATIC_ASSERT(EmptyValue < MaxValue);
  // STATIC_ASSERT(PreLockedValue < MaxValue);

  constexpr LockZBasicLockState(uintptr_t data) : _data(data) {}
  LockZBasicLockState(LockZNode* node);

  uintptr_t raw_data() const { return _data; }

  bool has_node() const;
  LockZNode* get_node() const;
  bool is_pre_locked() const;
  bool is_recursive() const;

  DEBUG_ONLY(bool is_bad() const;)
};

class LockZThreadData {
private:
  LockZNode* _pool;
  LockZNode* _locks;
  LockZNode* _park_state;
  size_t     _pool_estimate;

  LockZNode* alloc_node_inner();

  NONCOPYABLE(LockZThreadData);

public:
  static ByteSize pool_offset()  { return byte_offset_of(LockZThreadData, _pool); }
  static ByteSize locks_offset() { return byte_offset_of(LockZThreadData, _locks); }

  constexpr LockZThreadData() : _pool(nullptr), _locks(nullptr), _park_state(nullptr), _pool_estimate(0) {}
  ~LockZThreadData() {
    postcond(_locks == nullptr);
    postcond(_park_state == nullptr);
    postcond(_pool == nullptr);
  }

  LockZNode* park_state() const;
  void lock_park_state();

  LockZNode* alloc_park_state(oop object);
  LockZNode* alloc_and_lock_node(oop object);

  void pool_node(LockZNode* node);

  bool locks_contains(LockZNode* node) const;
  bool locks_contains(oop object) const;

  bool recursive_lock(oop object);

  void unlink_lock_node(LockZNode* node);

  LockZNode* get_lock_node(oop object);
  const LockZNode* get_lock_node(oop object) const;

  void clear_and_count_pool();
  void oops_do(OopClosure* cl);
};

class LockZSynchronizer : AllStatic {
private:
  static constexpr int spin_pause_log = 4;
  static constexpr int spin_thread_yield = 6;
  static LockZHashTable* _table;
  static LockZHashTable& get_hash_table();

  class VerifyEnter : StackObj {
  private:
    const Handle      _object;
    BasicLock* const  _lock;
    JavaThread* const _locking_thread;
    const bool        _is_vthread;

  public:
    VerifyEnter(oop object, BasicLock* lock, JavaThread* locking_thread);
    ~VerifyEnter();
  };

  class VerifyExit : StackObj {
  private:
    const oop         _object;
    BasicLock* const  _lock;
    JavaThread* const _current;
    const bool        _is_recursive;

  public:
    VerifyExit(oop object, BasicLock* lock, JavaThread* current);
    ~VerifyExit();
  };

  class VerifyNotify : StackObj {
  private:
    const Handle      _object;
    JavaThread* const _current;

  public:
    VerifyNotify(oop object, JavaThread* current);
    ~VerifyNotify();
  };

  class VerifyWait : StackObj {
  private:
    const Handle      _object;
    JavaThread* const _current;

  public:
    VerifyWait(oop object, JavaThread* current);
    ~VerifyWait();
  };

  template <typename Decider, typename Callback>
  static bool unlink_one_if(oop object, Decider&& decide, Callback&& callback);

  template <typename Callback>
  static void unlink_one(oop object, Callback&& callback);

  static bool check_owner(oop object, JavaThread* current);
  static void check_owner_or_imse(Handle handle, TRAPS);

  static bool fast_notify(oop object, JavaThread* current);
  static void slow_notify(Handle handle, bool notify_all, TRAPS);

  static bool fast_enter(oop object, BasicLock* lock, JavaThread* locking_thread);
  static void slow_enter(Handle handle, BasicLock* lock, JavaThread* current);

  static bool fast_exit(oop object, BasicLock* lock, JavaThread* current);
  static void slow_exit(oop object, JavaThread* current);

public:
  static void init_hash_table();

  template <bool notify_all, typename Scope>
  static void notify_in_scope(oop& object, Scope&& scope, TRAPS);
  static void notify(oop& object, TRAPS);
  static void notify_all(oop& object, TRAPS);

  static void wait(oop& object, TRAPS);

  // enter
  template <typename Scope>
  static void enter_in_scope(oop& object, BasicLock* lock, JavaThread* locking_thread, Scope&& scope);
  static void enter(oop& object, BasicLock* lock, JavaThread* locking_thread);
  static void jni_enter(oop& object, JavaThread* current);

  // exit
  template <typename Scope>
  static void exit_in_scope(oop& object, BasicLock* lock, JavaThread* current, Scope&& scope);
  static void exit(oop object, BasicLock* lock, JavaThread* current);
  static void jni_exit(oop object, JavaThread* current);
};

#endif // SHARE_RUNTIME_ZSYNCHRONIZER_HPP
