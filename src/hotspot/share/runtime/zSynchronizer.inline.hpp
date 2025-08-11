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

#ifndef SHARE_RUNTIME_ZSYNCHRONIZER_INLINE_HPP
#define SHARE_RUNTIME_ZSYNCHRONIZER_INLINE_HPP

#include "runtime/zSynchronizer.hpp"

#include "runtime/handles.inline.hpp"
#include "runtime/javaThread.inline.hpp"
#include "runtime/safepointVerifiers.hpp"
#include "utilities/macros.hpp"

inline Handle LockZSynchronizer::handle_helper(oop& object, Thread* current) {
  Handle object_handle(current, object);
  CHECK_UNHANDLED_OOPS_ONLY(object = nullptr;)
  return object_handle;
}

inline Handle LockZSynchronizer::handle_helper(Handle handle, Thread*) {
  return handle;
}

inline oop LockZSynchronizer::handle_helper(oop& object) {
  return object;
}

inline oop LockZSynchronizer::handle_helper(Handle handle) {
  return handle();
}

template <bool NotifyAll, typename oopOrHandle, typename Scope>
inline void LockZSynchronizer::notify_in_scope(oopOrHandle& object, Scope&& scope, TRAPS) {
  NoSafepointVerifier no_safepoint_verifier;
  DEBUG_ONLY(VerifyNotify verify(object, THREAD);)
  if (fast_notify(handle_helper(object), THREAD)) {
    return;
  }

  PauseNoSafepointVerifier pause(&no_safepoint_verifier);

  const auto slow = [&]() {
    Handle object_handle = handle_helper(object, THREAD);
    VerifyNotify verify(object_handle, THREAD);
    slow_notify(object_handle, NotifyAll, THREAD);
  };

  scope(slow);
}

template <typename oopOrHandle, typename Scope>
inline void LockZSynchronizer::enter_in_scope(oopOrHandle& object, BasicLock* lock, JavaThread* locking_thread, JavaThread* current, Scope&& scope) {
  NoSafepointVerifier no_safepoint_verifier;
  DEBUG_ONLY(VerifyEnter verify(object, lock, locking_thread);)
  if (fast_enter(handle_helper(object), lock, locking_thread)) {
    return;
  }

  PauseNoSafepointVerifier pause(&no_safepoint_verifier);

  const auto slow = [&]() {
    Handle object_handle = handle_helper(object, current);
    VerifyEnter verify(object_handle, lock, locking_thread);
    slow_enter<false>(object_handle, locking_thread);
  };

  scope(slow);
}

template <typename Scope>
inline void LockZSynchronizer::exit_in_scope(oop object, BasicLock* lock, JavaThread* current, Scope&& scope) {
  NoSafepointVerifier no_safepoint_verifier;
  VerifyExit verify(object, lock, current);
  if (fast_exit(object, lock, current)) {
    return;
  }

  const auto slow = [&]() {
    slow_exit(object, current);
  };

  scope(slow);
}

#endif // SHARE_RUNTIME_ZSYNCHRONIZER_INLINE_HPP
