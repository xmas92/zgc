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


#include "jfrfiles/jfrEventClasses.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/mutex.hpp"
#include "runtime/zSynchronizer.inline.hpp"

#include <atomic>
#include <utility>

class LockZNode;

class LockZMarkState {
private:
  std::atomic<uintptr_t>& _markword;

  static bool locked(uintptr_t value) {
    return (value & markWord::z_locked_mask_in_place) == markWord::z_locked_value;
  }

  static bool parker(uintptr_t value) {
    return (value & markWord::z_parked_mask_in_place) == markWord::z_parked_value;
  }

  static bool waiter(uintptr_t value) {
    return (value & markWord::z_waiter_mask_in_place) == markWord::z_waiter_value;
  }
public:
  LockZMarkState(oop object) : _markword(*reinterpret_cast<std::atomic<uintptr_t>*>(object->mark_addr())) {
    assert(_markword.is_lock_free(), "must be");
  }

  uintptr_t value() const {
    return _markword.load(std::memory_order_relaxed);
  }

  bool is_locked() const {
    return locked(value());
  }

  bool is_unlocked() const {
    return !is_locked();
  }

  bool has_parker() const {
    return parker(value());
  }

  bool has_waiter() const {
    return waiter(value());
  }

  void set_parker() {
    const uintptr_t or_value = markWord::z_parked_value;
    _markword.fetch_or(or_value, std::memory_order_release);
  }

  bool try_set_parker() const {
    uintptr_t expected = value();
    const uintptr_t desired = expected | markWord::z_parked_value;
    return _markword.compare_exchange_weak(expected, desired, std::memory_order_relaxed, std::memory_order_relaxed);
  }

  bool lock() {
    return (_markword.fetch_or(markWord::z_locked_value, std::memory_order_release) & markWord::z_locked_value) == 0;
  }

  bool unlock_if_no_parker() {
    uintptr_t expected = value() & ~markWord::z_parked_value;
    const uintptr_t desired = expected & ~markWord::z_locked_value;
    assert(locked(expected), "must be locked");
    return _markword.compare_exchange_strong(expected, desired, std::memory_order_release, std::memory_order_relaxed);
  }

  void clear_parker() {
    assert(has_parker(), "must be");
    const uintptr_t xor_value = markWord::z_parked_value;
    _markword.fetch_xor(xor_value, std::memory_order_release);
  }

  void unlock_with_parker(bool has_more) {
    assert(is_locked(), "must be");
    assert(has_parker(), "must be");

    const uintptr_t xor_value = markWord::z_locked_value | (has_more ? 0 : markWord::z_parked_value);
    _markword.fetch_xor(xor_value, std::memory_order_release);
  }

  void set_waiter() {
    assert(!has_waiter(), "must be");
    const uintptr_t or_value = markWord::z_waiter_value;
    _markword.fetch_or(or_value, std::memory_order_relaxed);
  }

  void clear_waiter() {
    assert(has_waiter(), "must be");
    const uintptr_t xor_value = markWord::z_waiter_value;
    _markword.fetch_xor(xor_value, std::memory_order_relaxed);
  }
};

struct LockZFastLock {
  oop _object;
  int _jni_lock_count;

  NOT_CHECK_UNHANDLED_OOPS(constexpr) LockZFastLock() = default;
  NOT_CHECK_UNHANDLED_OOPS(constexpr) explicit LockZFastLock(std::nullptr_t) : _object(nullptr), _jni_lock_count(0) {};
  NOT_CHECK_UNHANDLED_OOPS(constexpr) explicit LockZFastLock(oop object) : _object(object), _jni_lock_count(0) {};

  void init() { *this = LockZFastLock(nullptr); }
  void init(oop object) { *this = LockZFastLock(_object); }

  // _jni_lock_count keeps track of the number of JNI calls made,
  // its sign also keeps track of if a JNI call initiated the lock.
  //
  bool has_jni_lock_count() const { return _jni_lock_count != 0; }
  bool jni_initiated_lock() const { return _jni_lock_count < 0; }

  void initiate_lock_from_jni() {
    precond(!has_jni_lock_count());
    _jni_lock_count = -1;
  }

  void switch_to_jni_initiated_lock() {
    precond(!jni_initiated_lock());
    precond(!CheckJNICalls);
    _jni_lock_count *= -1;
  }

  void increment_jni_lock_count() {
    if (jni_initiated_lock()) {
      _jni_lock_count--;
    } else {
      _jni_lock_count++;
    }
  }

  void decrement_jni_lock_count() {
    if (jni_initiated_lock()) {
      _jni_lock_count++;
    } else {
      _jni_lock_count--;
    }
  }
};

enum class LockZParkState : uint8_t {
  // invalid default
  invalid,

  // Parked in Enter
  enter,
  // Parked in Wait - Waiting
  wait,
  // Parked in Wait - Notified
  notified,
};

class LockZParker {
private:
  union {
    OopHandle   _vthread;
    JavaThread* _thread;
  };
  bool _is_virtual;
  bool _initialized;

public:
  LockZParker()
    : _thread(nullptr),
      _is_virtual(false),
      _initialized(false) {}

  void init(JavaThread* thread) {
    precond(!_initialized);
    if (_is_virtual) {
      _vthread.release(JavaThread::thread_oop_storage());
      _vthread.~OopHandle();
    }
    _thread = thread;
    _is_virtual = false;
    _initialized = true;
  }

  void init(oop vthread) {
    precond(!_initialized);
    if (_is_virtual) {
      _vthread.replace(vthread);
    } else {
      // TODO[Axel]: Use other oop storage than JavaThread::thread_oop_storage()
      _vthread = OopHandle(JavaThread::thread_oop_storage(), vthread);
    }
    _is_virtual = true;
    _initialized = true;
  }

  ~LockZParker() {
    clear();
  }

  void clear() {
    if (_is_virtual) {
      _vthread.release(JavaThread::thread_oop_storage());
      _vthread.~OopHandle();
      _is_virtual = false;
    }
    _initialized = false;
  }

  ParkEvent* claim(oop& vthread) {
    precond(_initialized);
    _initialized = false;

    if (_is_virtual) {
      vthread = _vthread.resolve();
      _vthread.replace(nullptr);

      return ObjectMonitor::vthread_unparker_ParkEvent();
    }

    return _thread->_ParkEvent;
  }

  void park(jlong millis) const {
    precond(!_is_virtual);
    precond(_thread->thread_state() == _thread_blocked);
    if (millis == 0) {
      _thread->_ParkEvent->park();
    } else {
      _thread->_ParkEvent->park_nanos(millis_to_nanos(millis));
    }
  }

  void reset() const {
    precond(!_is_virtual);
    _thread->_ParkEvent->reset();
  }
};

class LockZContendedLock {
private:
  intptr_t    _key;
  OopHandle   _handle;
  LockZParker _parker;
  LockZParkState _park_state;

  void init_object(oop object) {
    precond(_park_state == LockZParkState::invalid);
    _key = object->identity_hash();
    _handle.replace(object);
  }

public:
  LockZContendedLock()
    : _key(0),
      _handle(JavaThread::thread_oop_storage(), nullptr),
      _parker(),
      _park_state(LockZParkState::invalid) {}

  ~LockZContendedLock() {
    _handle.release(JavaThread::thread_oop_storage());
  }

  intptr_t key() const { return _key; }
  oop object() const { return _handle.resolve(); }
  LockZParker& parker() { return _parker; }
  LockZParkState park_state() const { return _park_state; }

  void init_enter(oop object, oop vthread) {
    init_object(object);
    _parker.init(vthread);
    _park_state = LockZParkState::enter;
  }

  void init_enter(oop object, JavaThread* thread) {
    init_object(object);
    _parker.init(thread);
    _park_state = LockZParkState::enter;
  }

  void init_wait(oop object, oop vthread) {
    init_object(object);
    _parker.init(vthread);
    _park_state = LockZParkState::wait;
    // TODO: Not sure how vthread interputs work _parker.reset();
  }

  void init_wait(oop object, JavaThread* thread) {
    init_object(object);
    _parker.init(thread);
    _park_state = LockZParkState::wait;
    _parker.reset();
    // Is this fence needed?. How do we synch with the read of the thread
    // interrupt otherwise. But we do have a bucket lock and unlock in between.
    OrderAccess::fence();
  }

  void clear() {
    _handle.replace(nullptr);
    _parker.clear();
    DEBUG_ONLY(_park_state = LockZParkState::invalid;)
  }

  void notify() {
    precond(_park_state == LockZParkState::wait);
    _park_state = LockZParkState::notified;
  }
};

template <typename T, bool atomic>
class OptLockZ : public CHeapObj<MemTag::mtObjectMonitor>{
private:
  using bool_t = std::conditional_t<atomic, volatile bool, bool>;
  struct DummyData {
    constexpr DummyData() { /* avoid zero init */};
  };
  union {
    DummyData _dummy;
    T _data;
  };
  bool_t _constructed;

  public:
    constexpr OptLockZ() : _dummy(), _constructed(false) {}
    ~OptLockZ() { if (is_constructed()) { _data.~T(); } }

    constexpr void destroy() {
      precond(is_constructed());
      _data.~T();
      _constructed = false;
    }
    template <typename... Args>
    constexpr void create(Args&&... args) {
      precond(!is_constructed());
      ::new (&_data) T(std::forward<Args>(args)...);
      if (atomic) {
        Atomic::release_store(&_constructed, true);
      } else {
        _constructed = true;
      }
    }
    template <typename PreReleaseCallback, typename... Args>
    constexpr void create_pre_release(PreReleaseCallback&& callback, Args&&... args) {
      precond(!is_constructed());
      ::new (&_data) T(std::forward<Args>(args)...);
      callback(_data);
      if (atomic) {
        Atomic::release_store(&_constructed, true);
      } else {
        _constructed = true;
      }
    }

    constexpr T* operator->() { precond(is_constructed()); return &_data; }
    constexpr const T* operator->() const { precond(is_constructed()); return &_data; }
    constexpr T& operator*() { precond(is_constructed()); return _data; }
    constexpr const T& operator*() const { precond(is_constructed()); return _data; }
    constexpr explicit operator bool() const { return is_constructed(); }

    constexpr bool is_constructed() const {
      return atomic ? Atomic::load_acquire(&_constructed) : _constructed;
    }
};

class LockZNode : public CHeapObj<MemTag::mtObjectMonitor> {
private:
  using OptLockZContendedLock = OptLockZ<LockZContendedLock, false /* synced with bucket lock */>;

  LockZNode* _next;
  LockZNode* _prev;

  LockZFastLock         _fast_lock;
  OptLockZContendedLock _contented_lock;

  void ensure_contended_lock_constructed() {
    if (!has_contended()) {
      _contented_lock.create();
    }
  }

public:
  NOT_CHECK_UNHANDLED_OOPS(constexpr) explicit LockZNode() : _next(nullptr), _prev(nullptr) {}

  LockZNode* next() { return _next; }
  const LockZNode* next() const { return _next; }
  LockZNode* prev() { return _prev; }
  const LockZNode* prev() const { return _prev; }

  void single_unlink_node(LockZNode*& head) {
    if (_prev == nullptr) {
      head = _next;
    } else {
      _prev->_next = _next;
    }

    if (_next != nullptr) {
      _next->_prev = _prev;
    }
  }

  void double_unlink_node(LockZNode*& head, LockZNode*& tail) {
    if (_prev == nullptr) {
      head = _next;
    } else {
      _prev->_next = _next;
    }

    if (_next == nullptr) {
      tail = _prev;
    } else {
      _next->_prev = _prev;
    }
  }

  void single_link_head(LockZNode*& head) {
    precond(_next == nullptr);
    precond(_prev == nullptr);
    _next = head;
    head = this;
  }

  LockZNode* double_link_after_resize(LockZNode*& head, LockZNode* prev) {
    if (prev == nullptr) {
      head = this;
    } else {
      prev->_next = this;
    }
    _prev = prev;
    return this;
  }

  void double_link_head(LockZNode*& head) {
    precond(_next == nullptr);
    precond(_prev == nullptr);
    _next = head;
    if (_next != nullptr) {
      _next->_prev = this;
    }
    head = this;
  }

  void double_link_tail(LockZNode*& head, LockZNode*& tail) {
    precond(_next == nullptr);
    precond(_prev == nullptr);
    _prev = tail;
    if (_prev != nullptr) {
      _prev->_next = this;
    } else {
      assert(head == nullptr, "must be");
      head = this;
    }
    tail = this;
  }

  LockZFastLock& fast_lock() { return _fast_lock; }
  const LockZFastLock& fast_lock() const { return _fast_lock; }

  bool has_contended() const { return _contented_lock.is_constructed(); }

  void init_contended_lock_for_enter(oop object, oop vthread) {
    ensure_contended_lock_constructed();
    _contented_lock->init_enter(object, vthread);
  }

  void init_contended_lock_for_enter(oop object, JavaThread* thread) {
    ensure_contended_lock_constructed();
    _contented_lock->init_enter(object, thread);
  }

  void init_contended_lock_for_wait(oop vthread) {
    ensure_contended_lock_constructed();
    _contented_lock->init_wait(_fast_lock._object, vthread);
  }

  void init_contended_lock_for_wait(JavaThread* thread) {
    ensure_contended_lock_constructed();
    _contented_lock->init_wait(_fast_lock._object, thread);
  }

  LockZContendedLock& contented_lock() { return *_contented_lock; }
  const LockZContendedLock& contented_lock() const { return *_contented_lock; }
};

class LockZHashTable : public CHeapObj<MemTag::mtObjectMonitor> {
public:
  class LockZHashBucket {
    PlatformMutex _lock;
    LockZNode* _head;
    LockZNode* _tail;
    volatile bool _resized;

  public:
    LockZHashBucket() : _lock(), _head(nullptr), _tail(nullptr), _resized(false) {}
    bool try_lock() { return _lock.try_lock(); }
    void lock() { _lock.lock(); }
    void unlock() { _lock.unlock(); }
    bool resized() const { return Atomic::load(&_resized); }
    void resize(size_t next_size, LockZHashBucket& bucket_0, size_t index_0, LockZHashBucket& bucket_1, size_t index_1) {
      precond(!resized());

      LockZNode* prev_0 = nullptr;
      LockZNode* prev_1 = nullptr;

      // Claim head
      LockZNode* current = std::exchange(_head, nullptr);

      while (current != nullptr) {
        LockZNode* const next = current->next();
        const size_t index = size_t(current->contented_lock().key()) % next_size;
        if (index_0 == index) {
          prev_0 = current->double_link_after_resize(bucket_0._head, prev_0);
        } else {
          assert(index == index_1, "must be");
          prev_1 = current->double_link_after_resize(bucket_1._head, prev_1);
        }

        current = next;
      }

      // Clear tails
      _tail = nullptr;
      bucket_0._tail = prev_0;
      bucket_1._tail = prev_1;

      Atomic::store(&_resized, true);
    }

    struct UnlinkResult {
      LockZNode* _unlinked_node;
      bool _has_more;
    };

    template <typename Visitor>
    void visit(Visitor&& visit) {
      for (LockZNode* node = _head; node != nullptr; node = node->next()) {
        if (!visit(node->contented_lock())) {
          return;
        }
      }
    }

    template <typename Selector>
    UnlinkResult unlink_one(Selector&& select) {
      LockZNode* unlinked_node = nullptr;
      bool has_more = false;

      for (LockZNode* node = _head; node != nullptr; node = node->next()) {
        if (select(node->contented_lock())) {
          if (unlinked_node == nullptr) {
            unlinked_node = node;
            node = node->prev();
            node->double_unlink_node(_head, _tail);
          } else {
            has_more = true;
            break;
          }
        }
      }

      return {unlinked_node, has_more};
    }

    void link_one(LockZNode* node) {
      node->double_link_tail(_head, _tail);
    }
  };
  using OptLockZHashBucket = OptLockZ<LockZHashBucket, true /* may read without lock */>;

  class LockZHashTableData : public CHeapObj<MemTag::mtObjectMonitor> {
  private:
    const int           _size_class;
    volatile size_t     _resize_count;
    OptLockZHashBucket* _buckets;

  public:
    LockZHashTableData(int size_class) : _size_class(size_class), _resize_count(1 << size_class), _buckets() {
      _buckets = new OptLockZHashBucket[_resize_count];
    }

    constexpr OptLockZHashBucket& operator[](size_t index) {
      precond(index < (1 << _size_class));
      return _buckets[index];
    }

    void dec_resize_count() { Atomic::dec(&_resize_count); }
  };

  static constexpr int Log2LoadFactorReciprocal = 2;
  static constexpr size_t LoadFactorReciprocal = size_t(1) << Log2LoadFactorReciprocal;
  // Inital size = 2 / LoadFactor
  static constexpr int StartSizeClass = Log2LoadFactorReciprocal + 1;

  volatile bool _resize_requested;
  int _min_size_class;
  volatile int _current_size_class;
  volatile size_t _count;
  LockZHashTableData* volatile _data[32];

  template <bool ThreadShouldEnterBlocked>
  bool lock_bucket_or_process(LockZHashBucket& bucket) {
    bool processed = false;
    if (bucket.try_lock()) {
      // Locked successfully
    } else if (ThreadShouldEnterBlocked) {
      const auto on_pre_process = [&](JavaThread* current) {
        processed = true;
        bucket.unlock();
      };
      ThreadBlockInVMPreprocess<decltype(on_pre_process)> tbivm(JavaThread::current(), on_pre_process);
      bucket.lock();
    } else {
      bucket.lock();
    }
    return !processed;
  }

  template <bool ThreadShouldEnterBlocked>
  LockZHashBucket& get_bucket_locked_slow(intptr_t hash) {
  retry:
    for (int size_class = _min_size_class; size_class <= current_size_class(); ++size_class) {
      assert(Atomic::load(&_data[size_class]) != nullptr, "must exist");

      const size_t size = size_t(1) << size_class;
      const size_t index = size_t(hash) % size;
      LockZHashTableData& data = *Atomic::load_acquire(&_data[size_class]);
      LockZHashBucket* bucket = &*data[index];

      if (bucket->resized()) {
        // Bucket already resized
        continue;
      }

      if (!lock_bucket_or_process<ThreadShouldEnterBlocked>(*bucket)) {
        // Lock failed due to processing, retry
        goto retry;
      }

      // Bucket locked here

      // Recheck after lock
      if (bucket->resized()) {
        // Bucket already resized
        bucket->unlock();
        continue;
      }

      const int max_size_class = current_size_class();

      // Construct and move the links up to the max_size_class
      for (; size_class < max_size_class; ++size_class) {
        const int next_size_class = size_class + 1;
        const size_t next_size = size_t(1) << next_size_class;
        const size_t new_index_0 = index;
        const size_t new_index_1 = index + next_size;

        LockZHashTableData& next_data = *Atomic::load_acquire(&_data[size_class]);
        OptLockZHashBucket& bucket_0 = next_data[new_index_0];
        OptLockZHashBucket& bucket_1 = next_data[new_index_1];

        const auto pre_release_lock = [](LockZHashBucket& new_bucket) {
          new_bucket.lock();
        };

        bucket_0.create_pre_release(pre_release_lock);
        bucket_1.create_pre_release(pre_release_lock);

        // All buckets are locked here
        bucket->resize(next_size, *bucket_0, new_index_0, *bucket_1, new_index_1);

        // Unlock the newly created bucket our hash is not in
        const size_t next_index = size_t(hash) % next_size;
        if (new_index_0 == next_index) {
          bucket_1->unlock();
        } else {
          assert(new_index_1 == next_index, "must be");
          bucket_0->unlock();
        }
        // Unlock the resized bucket
        bucket->unlock();

        // Set bucket to the locked newly created bucket
        bucket = (new_index_0 == next_index) ? &*bucket_0 : &*bucket_1;
      }

      return *bucket;
    }

    // We must manage to lock a not resized bucket above.
    ShouldNotReachHere();
  }

  int current_size_class() const {
    return Atomic::load(&_current_size_class);
  }

  size_t curent_bucket_count() const {
    return size_t(1) << current_size_class();
  }

  bool should_resize(size_t size) const {
    // Resize
    return (LoadFactorReciprocal / 2 * size) > curent_bucket_count();
  }

  bool should_request_resize(size_t size) const {
    return !resize_requested() && (LoadFactorReciprocal * size) > curent_bucket_count();
  }

  bool resize_requested() const {
    return Atomic::load(&_resize_requested);
  }

public:
  LockZHashTable()
    : _resize_requested(false),
      _min_size_class(StartSizeClass),
      _current_size_class(0),
      _count(0),
      _data{} {
    const size_t size = size_t(1) << _min_size_class;

    // Create the inital table data
    _data[_min_size_class] = new LockZHashTableData(_min_size_class);
    for (size_t i = 0; i < size; ++i) {
      // And construct each lock
      (*_data[_min_size_class])[i].create();
    }
  }

  template <bool ThreadShouldEnterBlocked>
  void inc_size() {
    const size_t new_size = Atomic::add(&_count, 1u);
    if (should_request_resize(new_size) && Service_lock->try_lock()) {
      Atomic::store(&_resize_requested, true);
      Service_lock->notify();
      Service_lock->unlock();
    }
  }

  void dec_size() {
    // We do not shrink the table
    Atomic::sub(&_count, 1u);
  }

  template <bool ThreadShouldEnterBlocked>
  LockZHashBucket& get_bucket_locked(intptr_t hash) {
    for (;;) {
      const int size_class = current_size_class();
      const size_t size = size_t(1) << size_class;
      const size_t index = size_t(hash) % size;
      LockZHashTableData& data = *Atomic::load_acquire(&_data[size_class]);
      OptLockZHashBucket& bucket = data[index];
      if (bucket) {
        if (!lock_bucket_or_process<ThreadShouldEnterBlocked>(*bucket)) {
          // Lock failed due to processing, retry
          continue;
        }

        // Bucket is locked here

        if (bucket->resized()) {
          // Bucket is resized, retry
          bucket->unlock();
          continue;
        }

        return *bucket;
      }

      // Not yet constructed, take slow path without any lock
      return get_bucket_locked_slow<ThreadShouldEnterBlocked>(hash);
    }
  }
};

LockZNode* LockZThreadData::alloc_node_inner() {
  if (_pool == nullptr) {
    return new LockZNode();
  }

  LockZNode* const ret = _pool;
  _pool = ret->next();

  return ret;
}


LockZNode* LockZThreadData::park_state() const {
  return _park_state;
}

void LockZThreadData::lock_park_state() {
  precond(_park_state != nullptr);
  LockZNode* const node = std::exchange(_park_state, nullptr);
  node->double_link_head(_locks);
  postcond(locks_contains(node));
}

void LockZThreadData::set_wait_node(LockZNode* node) {
  precond(_park_state == nullptr);
  precond(node->contented_lock().park_state() == LockZParkState::wait);
  unlink_lock_node(node);
  _park_state = node;
}

LockZNode* LockZThreadData::alloc_park_state(oop object) {
  precond(_park_state == nullptr);
  LockZNode* const node = alloc_node_inner();
  node->fast_lock().init(object);
  _park_state = node;
  return node;
}

LockZNode* LockZThreadData::alloc_and_lock_node(oop object) {
  LockZNode* const node = alloc_node_inner();
  node->fast_lock().init(object);
  node->double_link_head(_locks);
  return node;
}

void LockZThreadData::pool_node(LockZNode* node) {
  CHECK_UNHANDLED_OOPS_ONLY(node->fast_lock().init());
  node->single_link_head(_pool);
}

bool LockZThreadData::locks_contains(LockZNode* node) const {
  for (LockZNode* current = _locks; current != nullptr; current = current->next()) {
    if (current == node) {
      return true;
    }
  }

  return false;
}

bool LockZThreadData::locks_contains(oop object) const {
  return get_lock_node(object) != nullptr;
}

bool LockZThreadData::recursive_lock(oop object) {
  LockZNode* const node = get_lock_node(object);

  if (node == nullptr) {
    return false;
  }

  return true;
}


void LockZThreadData::unlink_lock_node(LockZNode* node) {
  precond(locks_contains(node));
  node->single_unlink_node(_locks);
}

LockZNode* LockZThreadData::get_lock_node(oop object) {
  return const_cast<LockZNode*>(const_cast<const LockZThreadData*>(this)->get_lock_node(object));
}

const LockZNode* LockZThreadData::get_lock_node(oop object) const {
  for (LockZNode* current = _locks; current != nullptr; current = current->next()) {
    if (current->fast_lock()._object == object) {
      return current;
    }
  }

  return nullptr;
}

void LockZThreadData::clear_and_count_pool() {
  // TODO[Axel]: inline this
  size_t count = 0;
  LockZNode* next = _pool;
  while (next != nullptr && next->fast_lock()._object != nullptr) {
    ++count;
    _locks->fast_lock()._object = nullptr;
    next = next->next();
  }
  _pool_estimate += next != nullptr ? count : 0;

#ifdef ASSERT
  while (next != nullptr) {
    assert(_locks->fast_lock()._object == nullptr, "remaining should have be cleared");
    next = next->next();
  }
#endif
}

void LockZThreadData::oops_do(OopClosure* cl) {
  // TODO[Axel]: inline this
  cl->do_oop(&_park_state->fast_lock()._object);

  LockZNode* next = _locks;
  while (next != nullptr) {
    cl->do_oop(&_locks->fast_lock()._object);
    next = next->next();
  }
}

LockZBasicLockState::LockZBasicLockState(LockZNode* node)
  : _data(reinterpret_cast<uintptr_t>(node)) {}

bool LockZBasicLockState::has_node() const {
  return _data > ValueMask;
}

LockZNode* LockZBasicLockState::get_node() const {
  return reinterpret_cast<LockZNode*>(_data & ~ValueMask);
}

bool LockZBasicLockState::is_pre_locked() const {
  return _data == PreLockedValue;
}

bool LockZBasicLockState::is_recursive() const {
  return _data == RecursiveValue;
}

#ifdef ASSERT
bool LockZBasicLockState::is_bad() const {
  return _data == BadValue;
}
#endif // ASSERT

LockZHashTable* LockZSynchronizer::_table = nullptr;

LockZHashTable& LockZSynchronizer::get_hash_table() {
  precond(LockingMode == LM_LOCKZ);
  return *_table;
}

void LockZSynchronizer::VerifyEnter::verify_entry(oop object) {
  if (_locking_thread != JavaThread::current()) {
    guarantee(_locking_thread->is_suspendible_thread(), "other thread must be suspended");
  }

  const LockZBasicLockState lock_state = _lock->lock_z_basic_lock_state();
  guarantee(!lock_state.has_node(), "must not have a node");

  if (lock_state.is_pre_locked()) {
    const LockZMarkState mark_state(object);
    const LockZThreadData& thread_state = _locking_thread->lock_z_thread_data();

    guarantee(!thread_state.locks_contains(object), "should not contain lock");
    guarantee(mark_state.is_locked(), "must be locked");
  } else {
    assert(lock_state.is_bad(), "in debug we maintain a bad value invariant");
  }
}

LockZSynchronizer::VerifyEnter::VerifyEnter(oop object, BasicLock* lock, JavaThread* locking_thread)
  : VerifyEnter(Handle(), lock, locking_thread) {
  verify_entry(object);
}

LockZSynchronizer::VerifyEnter::VerifyEnter(Handle handle, BasicLock* lock, JavaThread* locking_thread)
  : _handle(handle),
    _lock(lock),
    _locking_thread(locking_thread),
    _is_vthread(_locking_thread->last_continuation() != nullptr &&
                _locking_thread->last_continuation()->is_virtual_thread()) {
  if (_handle.not_null()) {
    verify_entry(_handle());
  }
}

LockZSynchronizer::VerifyEnter::~VerifyEnter() {
  if (_handle.is_null()) {
    // Empty verifier
    return;
  }

  const LockZBasicLockState lock_state = _lock->lock_z_basic_lock_state();
  const LockZMarkState mark_state(_handle());
  const LockZThreadData& thread_state = _locking_thread->lock_z_thread_data();

  const bool is_preempted = _is_vthread /* || TODO[Add exact condition]*/;

  if (!is_preempted) {
    guarantee(mark_state.is_locked(), "must be locked");
    guarantee(thread_state.locks_contains(_handle()), "must contain lock");
    guarantee(lock_state.has_node(), "must have a node");

    guarantee(lock_state.get_node()->fast_lock()._object == _handle(), "lock node must be correct");
    guarantee(thread_state.get_lock_node(_handle()) == lock_state.get_node(), "thread node must be correct");
  }
}

LockZSynchronizer::VerifyExit::VerifyExit(oop object, BasicLock* lock, JavaThread* current)
  : _object(object),
    _lock(lock),
    _current(current),
    _is_recursive(_lock->lock_z_basic_lock_state().is_recursive()) {
  assert(current == JavaThread::current(), "must be");

  const LockZBasicLockState lock_state = _lock->lock_z_basic_lock_state();
  const LockZMarkState mark_state(_object);
  const LockZThreadData& thread_state = _current->lock_z_thread_data();

  guarantee(mark_state.is_locked(), "must be locked");
  guarantee(thread_state.locks_contains(_object), "must contain object");

  if (!_is_recursive) {
    guarantee(lock_state.has_node(), "none recursive exit must have node");
  }

}

LockZSynchronizer::VerifyExit::~VerifyExit() {
  const LockZBasicLockState lock_state = _lock->lock_z_basic_lock_state();
  const LockZThreadData& thread_state = _current->lock_z_thread_data();

  if (_is_recursive) {
    const LockZMarkState mark_state(_object);

    guarantee(mark_state.is_locked(), "must still be locked");
    guarantee(thread_state.locks_contains(_object), "must still contain");
  } else {
    guarantee(!thread_state.locks_contains(_object), "must not contain");
  }

  assert(lock_state.is_bad(), "in debug we maintain a bad value invariant");
}

LockZSynchronizer::VerifyNotify::VerifyNotify(oop object, JavaThread* current)
  : VerifyNotify(Handle(), current) {}

LockZSynchronizer::VerifyNotify::VerifyNotify(Handle handle, JavaThread* current)
  : _handle(handle),
    _current(current) {}

LockZSynchronizer::VerifyNotify::~VerifyNotify() {
}

LockZSynchronizer::VerifyWait::VerifyWait(Handle handle, JavaThread* current)
: _handle(handle),
  _current(current) {
}

LockZSynchronizer::VerifyWait::~VerifyWait() {
}

void LockZSynchronizer::init_hash_table() {
  precond(LockingMode == LM_LOCKZ);
  _table = new LockZHashTable();
}

#if INCLUDE_JFR
template <typename Event, typename... RemainingEvents>
class SizeOfEnabledEvents {
public:
  static size_t value() {
    using remaining_events_size = SizeOfEnabledEvents<RemainingEvents...>;
    const size_t event_size = jfr_is_event_enabled(Event::eventId) ? sizeof(Event) : 0;
    return event_size + remaining_events_size::value();
  }
};

template <typename Event>
class SizeOfEnabledEvents<Event> {
public:
  static size_t value() {
    return jfr_is_event_enabled(Event::eventId) ? sizeof(Event) : 0;
  }
};
#endif // INCLUDE_JFR

template <typename... Events>
static void ensure_jfr_event_buffer_capacity(JavaThread* current) {
#if INCLUDE_JFR
  using size_of_enabled_events_t = SizeOfEnabledEvents<Events...>;
  const size_t size = size_of_enabled_events_t::value();
  if (size > 0) {
    jfr_conditional_flush((JfrEventId)-1, size, current);
  }
#endif // INCLUDE_JFR
}

bool LockZSynchronizer::check_owner(oop object, JavaThread* current) {
  LockZThreadData& thread_state = current->lock_z_thread_data();
  return thread_state.locks_contains(object);
}

void LockZSynchronizer::check_owner_or_imse(Handle handle, TRAPS) {
  if (!check_owner(handle(), THREAD)) {
    THROW_MSG(vmSymbols::java_lang_IllegalMonitorStateException(),
              "current thread is not owner");
  }
}

bool LockZSynchronizer::fast_notify(oop object, JavaThread* current) {
  if (!check_owner(object, current)) {
    // Not owner, throw imse in slow path
    return false;
  }

  LockZMarkState mark_state(object);
  // Is there a waiter to notify?
  return !mark_state.has_waiter();
}

void LockZSynchronizer::slow_notify(Handle handle, bool notify_all, TRAPS) {
  assert(!handle->fast_no_hash_check(), "must have hash if waiter exists");

  // Throw if not owner
  check_owner_or_imse(handle, CHECK);

  const intptr_t key = handle->identity_hash();
  LockZHashTable& table = get_hash_table();

  auto& bucket = table.get_bucket_locked<true /* ThreadShouldEnterBlocked */>(key);

  oop object = handle();
  LockZMarkState mark_state(object);

  if (!mark_state.has_waiter()) {
    // There are no waiters, may have timed out
    bucket.unlock();
    return;
  }

  bool notified = false;
  bool has_more = false;
  bucket.visit([&](LockZContendedLock& lock) {
    if (lock.key() != key) {
      // Different keys, continue visiting
      return true;
    }

    if (lock.park_state() != LockZParkState::wait) {
      // Node is not waiting, continue visiting
      return true;
    }

    if (lock.object() != object) {
      // Different objects, continue visiting
      return true;
    }

    if (notified && !notify_all) {
      // Found a second waiter, set has_more and stop visiting
      has_more = true;
      return false;
    }

    // Notify the lock, continue visiting
    lock.notify();
    notified = true;
    return true;
  });

  assert(notified, "must have notified");

  if (!has_more) {
    // No more waiters, clear the waiter bit
    mark_state.clear_waiter();
  }

  if (!mark_state.has_parker()) {
    // We notified a waiter, but there were no parkers, set park bit
    mark_state.set_parker();
  }

  bucket.unlock();
}

void LockZSynchronizer::notify(Handle handle, TRAPS) {
  notify_in_scope<false /* notify_all */>(handle, [](auto slow){ slow(); }, THREAD);
}

void LockZSynchronizer::notify_all(Handle handle, TRAPS) {
  notify_in_scope<true /* notify_all */>(handle, [](auto slow){ slow(); }, THREAD);
}

void LockZSynchronizer::wait(Handle handle, jlong millis, bool interruptible, TRAPS) {
  VerifyWait verify(handle, THREAD);

  // Throw if not owner
  check_owner_or_imse(handle, CHECK);

    // check for a pending interrupt
  if (interruptible && THREAD->is_interrupted(true) && !HAS_PENDING_EXCEPTION) {
    THROW(vmSymbols::java_lang_InterruptedException());
    return;
  }

  LockZThreadData& thread_state = THREAD->lock_z_thread_data();
  LockZNode* const node = thread_state.get_lock_node(handle());

  OptLockZ<freeze_result, false /* thread local */> result;
  ContinuationEntry* const continuation_entry = THREAD->last_continuation();
  const bool is_vthread = continuation_entry != nullptr && continuation_entry->is_virtual_thread();

  // Create the waiter
  if (is_vthread) {
    node->init_contended_lock_for_wait(THREAD->vthread());
  } else {
    node->init_contended_lock_for_wait(THREAD);
  }

  // ensure_jfr_event_buffer_capacity<EventJavaMonitorWait, EventVirtualThreadPinned>(THREAD);
  // EventJavaMonitorWait wait_event;
  // EventVirtualThreadPinned vthread_pinned_event;

  const intptr_t key = handle->identity_hash();
  LockZHashTable& table = get_hash_table();

  { // Add waiter and unlock
    LockZHashTable::LockZHashBucket& bucket = table.get_bucket_locked<true>(key);
    LockZMarkState mark_state(handle());
    bucket.link_one(node);
    if (!mark_state.has_waiter()) {
      mark_state.set_waiter();
    }

    thread_state.set_wait_node(node);

    unlink_one_locked(handle(), key, bucket, [&](const auto &unlink_result) {
      LockZMarkState mark_state(handle());
      mark_state.unlock_with_parker(unlink_result._has_more);
    });
  }

  if (is_vthread) {
    result.create(Continuation::try_preempt(THREAD, continuation_entry->cont_oop(THREAD)));
    if (*result == freeze_result::freeze_ok) {
      Unimplemented();
      return;
    }
  }

  if (!interruptible || !THREAD->is_interrupted(false)) {
    // Park if we have not been interrupted.
    park_and_unlink_on_suspend(handle, millis, node, THREAD);
  }

  bool was_notified = true;

  LockZHashTable::LockZHashBucket& bucket = table.get_bucket_locked<true>(key);

  bool has_more = false;
  oop object = handle();
  auto unlink_result = bucket.unlink_one([&](const LockZContendedLock &lock) {
    if (lock.key() != key && lock.object() != object) {
      // Different keys and object
      return false;
    }

    if (&lock == &node->contented_lock()) {
      // Found our node
      was_notified = lock.park_state() == LockZParkState::notified;
      return true;
    }

    // Are there more waiters?
    has_more |= lock.park_state() == LockZParkState::wait;

    return false;
  });

  if (!was_notified && !has_more) {
    // This node was the last waiter and we removed ourself.
    LockZMarkState mark_state(object);
    mark_state.clear_waiter();
  }

  // Must only have found our own node
  postcond(unlink_result._unlinked_node == node || unlink_result._unlinked_node == nullptr);
  postcond(!unlink_result._has_more);

  // Unlock the bucket.
  bucket.unlock();

  // We are now unlocked and not on the wait list, perform an enter
  slow_enter<true>(handle, THREAD);

  // Throw interrupt exception incase we were interrupted and not notified.
  if (!was_notified && interruptible && THREAD->is_interrupted(true) && !HAS_PENDING_EXCEPTION) {
    THROW(vmSymbols::java_lang_InterruptedException());
  }
}

void LockZSynchronizer::wait(Handle handle, jlong millis, TRAPS) {
  wait(handle, millis, true, THREAD);
}

void LockZSynchronizer::wait_uninterruptible(Handle handle, jlong millis, TRAPS) {
  wait(handle, millis, false, THREAD);
}

bool LockZSynchronizer::fast_enter(oop object, BasicLock* lock, JavaThread* locking_thread) {
  const LockZBasicLockState lock_state = lock->lock_z_basic_lock_state();
  LockZThreadData& thread_state = locking_thread->lock_z_thread_data();

  const auto on_lock = [&]() {
    assert(!thread_state.locks_contains(object), "do not allow this for now");
    assert(LockZMarkState(object).is_locked(), "must be locked");
    LockZNode* node = thread_state.alloc_and_lock_node(object);
    lock->set_lock_z_basic_lock_state(LockZBasicLockState(node));
  };

  if (lock_state.is_pre_locked()) {
    // We get here if we have locked somewhere we could not easily allocate the
    // LockZNode
    on_lock();
    return true;
  }

  LockZMarkState mark_state(object);

  if (mark_state.lock()) {
    on_lock();
    return true;
  } else if (thread_state.recursive_lock(object)) {
    // Recursively locked
    return true;
  }

  // Short spin if there is no parker

  for (int spin_log = 0; spin_log < spin_pause_log && !mark_state.has_parker(); ++spin_log) {
    if (mark_state.lock()) {
      on_lock();
      return true;
    }

    for (int spin = 1 << spin_log; spin >= 0; --spin) {
      SpinPause();
    }
  }

  // Failed to fast lock
  return false;
}

template <bool is_wait_reenter>
void LockZSynchronizer::slow_enter(Handle handle, JavaThread* current) {
  // TODO: Use fast_lock as handle instead of handle
  assert(current == JavaThread::current(), "should never slow lock another thread");

  LockZThreadData& thread_state = current->lock_z_thread_data();
  LockZNode* const node = is_wait_reenter ? thread_state.park_state() : thread_state.alloc_park_state(handle());

  assert(!thread_state.locks_contains(handle()), "should have called fast_enter");

  // TODO[Axel]: Fix Events
  // ensure_jfr_event_buffer_capacity<EventJavaMonitorEnter, EventVirtualThreadPinned>(current);
  // EventJavaMonitorEnter enter_event;
  // EventJavaMonitorWait wait_event;
  // EventVirtualThreadPinned vthread_pinned_event;

  OptLockZ<freeze_result, false /* thread local */> result;
  ContinuationEntry* const continuation_entry = current->last_continuation();
  const bool is_vthread = continuation_entry != nullptr && continuation_entry->is_virtual_thread();
  bool contended_lock_initalized = false;

  const auto on_fast_lock = [&]() {
    if (contended_lock_initalized) {
      node->contented_lock().clear();
    }

    if (!result || *result != freeze_ok) {
      // We have not preempted, just return normally
      assert(LockZMarkState(handle()).is_locked(), "must be locked");

      thread_state.lock_park_state();
      return;
    }

    guarantee(is_vthread, "we should only have a freeze_result if we are a vthread");
    // TODO[Axel]: We have fast locked after having frozen our frames and thread
    //             state. So we must create a signal to know that the node that
    ///            was frozen, can be thread_state.lock_park_state(); once thawed.
  };

  for (;;) {
    { // SpinPause
      NoSafepointVerifier no_safepoint_verifier;
      LockZMarkState mark_state(handle());

      if (mark_state.lock()) {
        on_fast_lock();
        return;
      }

      for (int spin_log = 0; spin_log < spin_pause_log && !mark_state.has_parker(); ++spin_log) {
        for (int spin = 1 << spin_log; spin >= 0; --spin) {
          SpinPause();
        }

        if (mark_state.lock()) {
          on_fast_lock();
          return;
        }
      }
    }

    { // Thread Yield
      for (int yield_count = 0; yield_count < spin_thread_yield && !LockZMarkState(handle()).has_parker(); ++yield_count) {
        {
          ThreadBlockInVM tbivm(current);
          os::naked_yield();
        }

        LockZMarkState mark_state(handle());

        if (mark_state.has_parker()) {
          break;
        }

        if (mark_state.lock()) {
          on_fast_lock();
          return;
        }
      }
    }

    { // Start parking
      LockZMarkState mark_state(handle());
      if (!mark_state.try_set_parker()) {
        // Interference, yield and retry
        ThreadBlockInVM tbivm(current);
        os::naked_yield();
        continue;
      }
    }

    if (is_vthread && !result) {
      // Before parking we must freeze our frames
      result.create(Continuation::try_preempt(current, continuation_entry->cont_oop(current)));
    }

    const intptr_t key = node->contented_lock().key();
    LockZHashTable& table = get_hash_table();

    if (result && *result == freeze_ok) {
      assert(!contended_lock_initalized, "should not happen for vthread");
      node->init_contended_lock_for_enter(handle(), current->vthread());
      contended_lock_initalized = true;
      // TODO[Axel]: Verify this, assume we cannot safepoint after having frozen frames
      NoSafepointVerifier no_safepoint_verifier;
      LockZMarkState mark_state(handle());

      for (;;) {
        // Try to lock
        if (mark_state.lock()) {
          on_fast_lock();
          return;
        }

        // Retry parking
        if (!mark_state.try_set_parker()) {
          continue;
        }

        auto& bucket = table.get_bucket_locked<false>(key);
        if (!mark_state.has_parker() || mark_state.is_unlocked()) {
          // Something has changed after we got the bucket lock, unlock and retry
          bucket.unlock();
          continue;
        }

        // Link our node
        bucket.link_one(node);
        bucket.unlock();

        // We are on the enter queue, fallout to preempted stub.
        return;
      }

      // The loop above is terminal.
      ShouldNotReachHere();
    }

    if (!contended_lock_initalized) {
      node->init_contended_lock_for_enter(handle(), current);
      contended_lock_initalized = true;
    }

    node->contented_lock().parker().reset();

    auto& bucket = table.get_bucket_locked<true>(key);
    LockZMarkState mark_state(handle());
    if (!mark_state.has_parker() || mark_state.is_unlocked()) {
      // Something has changed after we got the bucket lock, unlock and retry
      bucket.unlock();
      continue;
    }

    // Link our node
    bucket.link_one(node);
    bucket.unlock();

    park_and_unlink_on_suspend(handle, 0, node, current);
    // We have been unlinked, try locking again
  }
}

void LockZSynchronizer::enter(Handle handle, BasicLock* lock, JavaThread* locking_thread, JavaThread* current) {
  enter_in_scope(handle, lock, locking_thread, current, [](auto slow) { slow(); });
}

bool LockZSynchronizer::fast_exit(oop object, BasicLock* lock, JavaThread* current) {
  const LockZBasicLockState lock_state = lock->lock_z_basic_lock_state();
  LockZThreadData& thread_state = current->lock_z_thread_data();

  if (lock_state.is_recursive()) {
    // Recursive unlock does nothing
    DEBUG_ONLY(lock->set_lock_z_basic_lock_state(LockZBasicLockState::bad_state());)
    return true;
  }

  LockZNode* node = lock_state.get_node();
  DEBUG_ONLY(lock->set_lock_z_basic_lock_state(LockZBasicLockState::bad_state());)

  if (node->fast_lock().has_jni_lock_count()) {
    // Unbalanced locking
    if (CheckJNICalls) {
      fatal("JNI unbalanced locking");
    }

    // We treat this is a recursive unlock, and switch the lock to a JNI
    // initiated one.
    node->fast_lock().switch_to_jni_initiated_lock();
    return true;
  }

  // Unlink the node
  thread_state.unlink_lock_node(node);

  // Try to unlock the object
  LockZMarkState mark_state(object);
  return mark_state.unlock_if_no_parker();
}


template <typename Bucket, typename Callback>
void LockZSynchronizer::unlink_one_locked(oop object, intptr_t key, Bucket& bucket, Callback&& callback) {
auto unlink_result = bucket.unlink_one([&](const LockZContendedLock &lock) {
    if (lock.key() != key) {
      // Different keys
      return false;
    }

    if (lock.park_state() != LockZParkState::enter &&
        lock.park_state() != LockZParkState::notified) {
      // Not a valid state to wake up
      return false;
    }

    if (lock.object() != object) {
      // Different objects
      return false;
    }

    return true;
  });

  // Evaluate callback
  callback(unlink_result);

  if (unlink_result._unlinked_node != nullptr) {
    oop vthread = nullptr;
    ParkEvent* trigger = nullptr;

    trigger = unlink_result._unlinked_node->contented_lock().parker().claim(vthread);

    // Unlock the bucket
    bucket.unlock();

    // Unpark the unlinked node
    if (vthread == nullptr ||
        java_lang_VirtualThread::set_onWaitingList(
            vthread, ObjectMonitor::vthread_list_head())) {
      trigger->unpark();
    }
  } else {
    // Unlock the bucket
    bucket.unlock();
  }
}

template <typename Decider, typename Callback>
bool LockZSynchronizer::unlink_one_if(oop object, Decider&& decide, Callback&& callback) {
  LockZHashTable& table = get_hash_table();

  const intptr_t key = object->identity_hash();
  auto& bucket = table.get_bucket_locked<false>(key);

  if (!decide()) {
    // Decided not to unlink

    // Unlock bucket and return
    bucket.unlock();
    return false;
  }

  unlink_one_locked(object, key, bucket, callback);

  return true;
}

template <typename Callback>
void LockZSynchronizer::unlink_one(oop object, Callback&& callback) {
  // Always decide to unlink
  unlink_one_if(object, []() { return true; }, callback);
}

void LockZSynchronizer::park_and_unlink_on_suspend(Handle handle, jlong millis, LockZNode* node, JavaThread* current) {
    const auto on_suspend = [&](JavaThread *current) {
      // If we are suspended here, we must ensure that another thread wakes up
      LockZMarkState mark_state(handle());
      unlink_one_if(
          handle(),
          [&]() {
            // Only try and unlink
            return mark_state.has_parker();
          },
          [&](const auto &unlink_result) {
            if (!unlink_result._has_more) {
              mark_state.clear_parker();
            }
          });
    };
    ThreadBlockInVMPreprocess<decltype(on_suspend)> tbivm(current, on_suspend, true);
    node->contented_lock().parker().park(millis);
}

void LockZSynchronizer::slow_exit(oop object, JavaThread* current) {
#ifdef ASSERT
  LockZThreadData& thread_state = current->lock_z_thread_data();
  assert(!thread_state.locks_contains(object), "was fast_exit called?");
#endif

  unlink_one(object, [&](const auto &unlink_result) {
    LockZMarkState mark_state(object);
    mark_state.unlock_with_parker(unlink_result._has_more);
  });
}

void LockZSynchronizer::exit(oop object, BasicLock* lock, JavaThread* current) {
  exit_in_scope(object, lock, current, [](auto slow) { slow(); });
}

void LockZSynchronizer::jni_enter(Handle handle, JavaThread* current) {
  // Find lock node if it exists
  LockZThreadData& thread_state = current->lock_z_thread_data();
  LockZNode* const node = thread_state.get_lock_node(handle());

  // Lock on object
  BasicLock lock;
  enter(handle, &lock, current, current);

  if (node == nullptr) {
    // We had no lock node before enter, this is a jni initiated lock.
    const LockZBasicLockState lock_state = lock.lock_z_basic_lock_state();
    lock_state.get_node()->fast_lock().initiate_lock_from_jni();
  } else {
    // Otherwise just increment the lock count
    node->fast_lock().increment_jni_lock_count();
  }
}

void LockZSynchronizer::jni_exit(oop object, JavaThread* current) {
  // Find lock node
  LockZThreadData& thread_state = current->lock_z_thread_data();
  LockZNode* node = thread_state.get_lock_node(object);

  if (node == nullptr || !node->fast_lock().has_jni_lock_count()) {
    // Unbalanced unlocking.
    if (CheckJNICalls) {
      fatal("JNI unbalanced locking");
    }
    // TODO[Axel]: Check this JNI logic
    return;
  }

  // Update and keep track of the jni locking state
  const bool jni_initiated_lock = node->fast_lock().jni_initiated_lock();
  node->fast_lock().decrement_jni_lock_count();

  const bool is_recursive = !jni_initiated_lock || node->fast_lock().has_jni_lock_count();

  // Setup the correct BasicLock
  BasicLock lock;
  if (is_recursive) {
    lock.set_lock_z_basic_lock_state(LockZBasicLockState::recursive_state());
  } else {
    lock.set_lock_z_basic_lock_state(LockZBasicLockState(node));
  }

  // Unlock the object
  exit(object, &lock, current);
}
