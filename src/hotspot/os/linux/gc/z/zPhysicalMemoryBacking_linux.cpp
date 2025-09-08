/*
 * Copyright (c) 2015, 2025, Oracle and/or its affiliates. All rights reserved.
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

#include "gc/shared/gcLogPrecious.hpp"
#include "gc/shared/gc_globals.hpp"
#include "gc/z/zAddress.inline.hpp"
#include "gc/z/zArray.inline.hpp"
#include "gc/z/zErrno.hpp"
#include "gc/z/zGlobals.hpp"
#include "gc/z/zInitialize.hpp"
#include "gc/z/zLargePages.inline.hpp"
#include "gc/z/zMountPoint_linux.hpp"
#include "gc/z/zNUMA.inline.hpp"
#include "gc/z/zPhysicalMemoryBacking_linux.hpp"
#include "gc/z/zPhysicalMemoryManager.hpp"
#include "gc/z/zRange.inline.hpp"
#include "gc/z/zSyscall_linux.hpp"
#include "hugepages.hpp"
#include "logging/log.hpp"
#include "memory/allocation.hpp"
#include "nmt/memTag.hpp"
#include "os_linux.hpp"
#include "runtime/init.hpp"
#include "runtime/os.hpp"
#include "runtime/safefetch.hpp"
#include "utilities/align.hpp"
#include "utilities/debug.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>

//
// Support for building on older Linux systems
//

// memfd_create(2) flags
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC                      0x0001U
#endif
#ifndef MFD_HUGETLB
#define MFD_HUGETLB                      0x0004U
#endif
#ifndef MFD_HUGE_2MB
#define MFD_HUGE_2MB                     0x54000000U
#endif

// open(2) flags
#ifndef O_CLOEXEC
#define O_CLOEXEC                        02000000
#endif
#ifndef O_TMPFILE
#define O_TMPFILE                        (020000000 | O_DIRECTORY)
#endif

// fallocate(2) flags
#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE              0x01
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE             0x02
#endif

// Filesystem types, see statfs(2)
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC                      0x01021994
#endif
#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC                  0x958458f6
#endif

// mremap(2)
#ifndef MREMAP_DONTUNMAP
#define MREMAP_DONTUNMAP	               4
#endif

// madvise(2)
#ifndef MADV_FREE
#define MADV_FREE	                       8
#endif

// Filesystem names
#define ZFILESYSTEM_TMPFS                "tmpfs"
#define ZFILESYSTEM_HUGETLBFS            "hugetlbfs"

// Proc file entry for max map mount
#define ZFILENAME_PROC_MAX_MAP_COUNT     "/proc/sys/vm/max_map_count"

// Sysfs file for transparent huge page on tmpfs
#define ZFILENAME_SHMEM_ENABLED          "/sys/kernel/mm/transparent_hugepage/shmem_enabled"

// Java heap filename
#define ZFILENAME_HEAP                   "java_heap"

// Preferred tmpfs mount points, ordered by priority
static const char* ZPreferredTmpfsMountpoints[] = {
  "/dev/shm",
  "/run/shm",
  nullptr
};

// Preferred hugetlbfs mount points, ordered by priority
static const char* ZPreferredHugetlbfsMountpoints[] = {
  "/dev/hugepages",
  "/hugepages",
  nullptr
};

static int z_fallocate_hugetlbfs_attempts = 3;
static bool z_fallocate_supported = true;
char* ZPhysicalMemoryBacking::_reserved_anon_memory_mapping = nullptr;

ZPhysicalMemoryBacking::AnonymousMemoryMode::AnonymousMemoryMode()
  : _vma_crossing_mremap_supported(true),
    _commit_in_backing_space(true),
    _use_madv_free_supported(true) {}

bool ZPhysicalMemoryBacking::AnonymousMemoryMode::remap_whole_range() const {
  return Atomic::load(&_vma_crossing_mremap_supported);
}

bool ZPhysicalMemoryBacking::AnonymousMemoryMode::commit_in_backing_space() const {
  return Atomic::load(&_commit_in_backing_space);
}

bool ZPhysicalMemoryBacking::AnonymousMemoryMode::use_madv_free() const {
  return Atomic::load(&_use_madv_free_supported);
}

void ZPhysicalMemoryBacking::AnonymousMemoryMode::set_vma_crossing_mremap_unsupported() {
  Atomic::store(&_vma_crossing_mremap_supported, false);
}

void ZPhysicalMemoryBacking::AnonymousMemoryMode::commit_in_backing_space_failed() {
  Atomic::store(&_commit_in_backing_space, false);
}

void ZPhysicalMemoryBacking::AnonymousMemoryMode::set_madv_free_unsupported() {
  Atomic::store(&_use_madv_free_supported, false);
}

bool ZPhysicalMemoryBacking::reserve_anon_memory_mapping(size_t max_capacity) {
  _reserved_anon_memory_mapping = (char*)mmap(0, max_capacity, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  if (_reserved_anon_memory_mapping == MAP_FAILED) {
    _reserved_anon_memory_mapping = nullptr;
    return false;
  }

  return true;
}

bool ZPhysicalMemoryBacking::check_for_madv_free_support() {
  if (!ZUncommit) {
    // Cannot uncommit, should never free memery, so set madv_free as unsupported
    _anonymous_memory_mode.set_madv_free_unsupported();

    return true;
  }

  if (ZLargePages::is_explicit()) {
    // madv free does not work for Huge TLB
    _anonymous_memory_mode.set_madv_free_unsupported();

    return true;
  }

  // Reserve some memory
  const size_t size = ZGranuleSize;
  void* const addr_start = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr_start == MAP_FAILED) {
    ZErrno err;
    ZInitialize::error("mmap failed while checking for madv_free support (%s)", err.to_string());
    return false;
  }
  // Page in memory
  void* const addr_end = (char*)addr_start + size;
  os::pretouch_memory(addr_start, addr_end);

  // Try MADV_FREE
  if (madvise(addr_start, size, MADV_FREE) == -1) {
    ZErrno err;
    log_debug_p(gc, init)("Heap Backing: MADV_FREE unsupported (%s)", err.to_string());
    _anonymous_memory_mode.set_madv_free_unsupported();
  } else {
    assert(_anonymous_memory_mode.use_madv_free(), "should be default");
    log_debug_p(gc, init)("Heap Backing: Using asynchronous uncommit MADV_FREE");
  }

  // Unmap the reservation (freeing memory is madvise failed)
  if (munmap(addr_start, size) == -1) {
    ZErrno err;
    ZInitialize::error("munmap failed while checking for madv_free support (%s)", err.to_string());
    return false;
  }

  return true;
}

bool ZPhysicalMemoryBacking::check_for_vma_crossing_mremap_support() {
#define Z_CHECK_MAP_ERROR(function, fail_value, value, variable)                                                       \
  do {                                                                                                                 \
    if (value == fail_value) {                                                                                         \
      ZErrno err;                                                                                                      \
      ZInitialize::error(#function " failed while checking for mremap support (" #variable ") (%s)", err.to_string()); \
      return false;                                                                                                    \
    }                                                                                                                  \
  } while (false)
#define Z_CHECK_MMAP_ERROR(addr) Z_CHECK_MAP_ERROR(mmap, MAP_FAILED, addr, addr)
#define Z_MUNMAP_AND_CHECK_ERROR(addr, size) Z_CHECK_MAP_ERROR(munmap, -1, (munmap(addr, size)), addr)

  const size_t num_parts = 2;
  const size_t part_size = ZGranuleSize;
  const size_t full_size = num_parts * part_size;

  // Reserve Two memory ranges
  void* const addr_space_1 = mmap(nullptr, full_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  Z_CHECK_MMAP_ERROR(addr_space_1);
  void* const addr_space_2 = mmap(nullptr, full_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  Z_CHECK_MMAP_ERROR(addr_space_2);

  // Map, page in and remap all parts into addr_space_1
  for (size_t i = 0; i < num_parts; i++) {
    void* const addr_part_start = mmap(nullptr, part_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Z_CHECK_MMAP_ERROR(addr_part_start);
    void* const addr_part_end = (char*)addr_part_start + part_size;
    os::pretouch_memory(addr_part_start, addr_part_end);
    // Map them in reverse order in case memory order could facilitate VMA coalescing
    const size_t move_to_part_index = num_parts - i - 1;
    void* const move_to_addr = (char*)addr_space_1 + move_to_part_index * part_size;
    if (mremap(addr_part_start, part_size, part_size, MREMAP_MAYMOVE | MREMAP_FIXED, move_to_addr) == MAP_FAILED) {
      ZErrno err;
      ZInitialize::error("mremap unexpectedly failed while checking for mremap support (%s)", err.to_string());
      return false;
    }
  }

  // Check if we can mremap the whole address space.
  if (mremap(addr_space_1, full_size, full_size, MREMAP_MAYMOVE | MREMAP_FIXED | MREMAP_DONTUNMAP, addr_space_2) == MAP_FAILED) {
    ZErrno err;
    log_debug_p(gc, init)("Heap Backing: Using legacy mremap (%s)", err.to_string());
    _anonymous_memory_mode.set_vma_crossing_mremap_unsupported();

    // The kernel may have unmapped addr_space_2 on failure, we do not munmap as
    // this process may have mmaped that that address space. We could try to
    // re-mmap it and see. But instead we just leak this small no-reserved region.
  } else {
    log_debug_p(gc, init)("Heap Backing: Using new mremap");
    assert(_anonymous_memory_mode.remap_whole_range(), "should be default");

    // Unmap and free addr_space_2
    Z_MUNMAP_AND_CHECK_ERROR(addr_space_2, full_size);
  }

  // Unmap and free addr_space_1
  Z_MUNMAP_AND_CHECK_ERROR(addr_space_1, full_size);

#undef Z_MUNMAP_AND_CHECK_ERROR
#undef Z_CHECK_MMAP_ERROR
#undef Z_CHECK_MAP_ERROR
  return true;
}

using ZBackingRange = ZRange<zbacking_offset, zbacking_offset_end>;

class ZBackingRangeNode : public CHeapObj<MemTag::mtGC> {
private:
  const ZBackingRange _range;
  char* const _new_backing_file;
  ZBackingRangeNode* _next;

public:
  ZBackingRangeNode(zbacking_offset offset, size_t length, char* new_backing_file)
    : _range(offset, length),
      _new_backing_file(new_backing_file),
      _next(nullptr) {}

  void set_next(ZBackingRangeNode* next) {
    _next = next;
  }

  ZBackingRangeNode* next() const {
    return  _next;
  }

  char* file_addr() const {
    return _new_backing_file;
  }

  const ZBackingRange& range() const {
    return _range;
  }
};

template <typename F>
void ZPhysicalMemoryBacking::for_offset_length_do_inner(zbacking_offset offset, size_t length, F&& f) const {
  const ZBackingRange range(offset, length);

  for (ZBackingRangeNode* next = Atomic::load_acquire(&_broken_physical_backing_head); next != nullptr; next = next->next()) {
    const ZBackingRange& broken_range = next->range();
    if (broken_range.overlaps(range)) {
      const bool has_pre_range = range.start() < broken_range.start();
      const bool has_post_range = range.end() > broken_range.end();

      if (has_pre_range) {
        // Handle pre range
        const zbacking_offset start = range.start();
        const zbacking_offset end = broken_range.start();
        const size_t length = end - start;
        for_offset_length_do_inner(start, length, f);
      }

      {
        // Handle Overlap
        const zbacking_offset start = MAX2(range.start(), broken_range.start());
        const zbacking_offset_end end = MIN2(range.end(), broken_range.end());
        const size_t offset_in_broken_range = start - broken_range.start();
        const size_t length = end - start;
        f(next->file_addr() + offset_in_broken_range, start, length);
      }

      if (has_post_range) {
        // Handle post range
        const zbacking_offset start = to_zbacking_offset(broken_range.end());
        const zbacking_offset_end end = range.end();
        const size_t length = end - start;
        for_offset_length_do_inner(start, length, f);
      }

      return;
    }
  }

  f(_physical_mapping + untype(range.start()), range.start(), range.size());
  return;
}

template <typename F>
void ZPhysicalMemoryBacking::for_offset_length_do(zbacking_offset offset, size_t length, F&& f) const {
  // Only anonymous can get broken physical backing files.
  postcond(is_anonymous());

  if (!Atomic::load(&_broken_physical_backing)) {
    // This thread has not observed any broken physical backings yet.
    f(_physical_mapping + untype(offset), offset, length);
    return;
  }

  for_offset_length_do_inner(offset, length, f);
}

ZPhysicalMemoryBacking::ZPhysicalMemoryBacking(size_t max_capacity)
  : _fd(-1),
    _filesystem(0),
    _block_size(0),
    _available(0),
    _physical_mapping(nullptr),
    _broken_physical_backing_head(nullptr),
    _broken_physical_backing(false),
    _anonymous_memory_mode(),
    _initialized(false) {

  if (ZAnonymousMemoryBacking) {
    guarantee(_reserved_anon_memory_mapping != nullptr, "anonymous memory backing does not exist");
    // Use anonymous memory backing when available, no filesystem needed
    _physical_mapping = _reserved_anon_memory_mapping;

    _block_size = ZGranuleSize;

    log_info_p(gc, init)("Heap Backing: Anonymous Memory");

    if (!check_for_madv_free_support()) {
      // Failed to check for MADV_FREE support
      return;
    }

    if (!check_for_vma_crossing_mremap_support()) {
      // Failed to check for mremap support
      return;
    }

    // Successfully initialized
    _initialized = true;
    return;
  }

  // Create backing file
  _fd = create_fd(ZFILENAME_HEAP);
  if (_fd == -1) {
    ZInitialize::error("Failed to create heap backing file");
    return;
  }

  // Truncate backing file
  while (ftruncate(_fd, max_capacity) == -1) {
    if (errno != EINTR) {
      ZErrno err;
      ZInitialize::error("Failed to truncate backing file (%s)", err.to_string());
      return;
    }
  }

  // Get filesystem statistics
  struct statfs buf;
  if (fstatfs(_fd, &buf) == -1) {
    ZErrno err;
    ZInitialize::error("Failed to determine filesystem type for backing file (%s)", err.to_string());
    return;
  }

  _filesystem = buf.f_type;
  _block_size = buf.f_bsize;
  _available = buf.f_bavail * _block_size;

  log_info_p(gc, init)("Heap Backing Filesystem: %s (" UINT64_FORMAT_X ")",
                       is_tmpfs() ? ZFILESYSTEM_TMPFS : is_hugetlbfs() ? ZFILESYSTEM_HUGETLBFS : "other", _filesystem);

  // Make sure the filesystem type matches requested large page type
  if (ZLargePages::is_transparent() && !is_tmpfs()) {
    ZInitialize::error("-XX:+UseTransparentHugePages can only be enabled when using a %s filesystem",
                       ZFILESYSTEM_TMPFS);
    return;
  }

  if (ZLargePages::is_transparent() && !tmpfs_supports_transparent_huge_pages()) {
    ZInitialize::error("-XX:+UseTransparentHugePages on a %s filesystem not supported by kernel",
                       ZFILESYSTEM_TMPFS);
    return;
  }

  if (ZLargePages::is_explicit() && !is_hugetlbfs()) {
    ZInitialize::error("-XX:+UseLargePages (without -XX:+UseTransparentHugePages) can only be enabled "
                       "when using a %s filesystem", ZFILESYSTEM_HUGETLBFS);
    return;
  }

  if (!ZLargePages::is_explicit() && is_hugetlbfs()) {
    ZInitialize::error("-XX:+UseLargePages must be enabled when using a %s filesystem",
                       ZFILESYSTEM_HUGETLBFS);
    return;
  }

  // Make sure the filesystem block size is compatible
  if (ZGranuleSize % _block_size != 0) {
    ZInitialize::error("Filesystem backing the heap has incompatible block size (%zu)",
                       _block_size);
    return;
  }

  if (is_hugetlbfs() && _block_size != ZGranuleSize) {
    ZInitialize::error("%s filesystem has unexpected block size %zu (expected %zu)",
                       ZFILESYSTEM_HUGETLBFS, _block_size, ZGranuleSize);
    return;
  }

  // Successfully initialized
  _initialized = true;
}

int ZPhysicalMemoryBacking::create_mem_fd(const char* name) const {
  assert(ZGranuleSize == 2 * M, "Granule size must match MFD_HUGE_2MB");

  // Create file name
  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "%s%s", name, ZLargePages::is_explicit() ? ".hugetlb" : "");

  // Create file
  const int extra_flags = ZLargePages::is_explicit() ? (MFD_HUGETLB | MFD_HUGE_2MB) : 0;
  const int fd = ZSyscall::memfd_create(filename, MFD_CLOEXEC | extra_flags);
  if (fd == -1) {
    ZErrno err;
    log_debug_p(gc, init)("Failed to create memfd file (%s)",
                          (ZLargePages::is_explicit() && (err == EINVAL || err == ENODEV)) ?
                          "Hugepages (2M) not available" : err.to_string());
    return -1;
  }

  log_info_p(gc, init)("Heap Backing File: /memfd:%s", filename);

  return fd;
}

int ZPhysicalMemoryBacking::create_file_fd(const char* name) const {
  const char* const filesystem = ZLargePages::is_explicit()
                                 ? ZFILESYSTEM_HUGETLBFS
                                 : ZFILESYSTEM_TMPFS;
  const char** const preferred_mountpoints = ZLargePages::is_explicit()
                                             ? ZPreferredHugetlbfsMountpoints
                                             : ZPreferredTmpfsMountpoints;

  // Find mountpoint
  ZMountPoint mountpoint(filesystem, preferred_mountpoints);
  if (mountpoint.get() == nullptr) {
    log_error_p(gc)("Use -XX:AllocateHeapAt to specify the path to a %s filesystem", filesystem);
    return -1;
  }

  // Try to create an anonymous file using the O_TMPFILE flag. Note that this
  // flag requires kernel >= 3.11. If this fails we fall back to open/unlink.
  const int fd_anon = os::open(mountpoint.get(), O_TMPFILE|O_EXCL|O_RDWR|O_CLOEXEC, S_IRUSR|S_IWUSR);
  if (fd_anon == -1) {
    ZErrno err;
    log_debug_p(gc, init)("Failed to create anonymous file in %s (%s)", mountpoint.get(),
                          (err == EINVAL ? "Not supported" : err.to_string()));
  } else {
    // Get inode number for anonymous file
    struct stat stat_buf;
    if (fstat(fd_anon, &stat_buf) == -1) {
      ZErrno err;
      log_error_pd(gc)("Failed to determine inode number for anonymous file (%s)", err.to_string());
      return -1;
    }

    log_info_p(gc, init)("Heap Backing File: %s/#" UINT64_FORMAT, mountpoint.get(), (uint64_t)stat_buf.st_ino);

    return fd_anon;
  }

  log_debug_p(gc, init)("Falling back to open/unlink");

  // Create file name
  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "%s/%s.%d", mountpoint.get(), name, os::current_process_id());

  // Create file
  const int fd = os::open(filename, O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC, S_IRUSR|S_IWUSR);
  if (fd == -1) {
    ZErrno err;
    log_error_p(gc)("Failed to create file %s (%s)", filename, err.to_string());
    return -1;
  }

  // Unlink file
  if (unlink(filename) == -1) {
    ZErrno err;
    log_error_p(gc)("Failed to unlink file %s (%s)", filename, err.to_string());
    return -1;
  }

  log_info_p(gc, init)("Heap Backing File: %s", filename);

  return fd;
}

int ZPhysicalMemoryBacking::create_fd(const char* name) const {
  if (AllocateHeapAt == nullptr) {
    // If the path is not explicitly specified, then we first try to create a memfd file
    // instead of looking for a tmpfd/hugetlbfs mount point. Note that memfd_create() might
    // not be supported at all (requires kernel >= 3.17), or it might not support large
    // pages (requires kernel >= 4.14). If memfd_create() fails, then we try to create a
    // file on an accessible tmpfs or hugetlbfs mount point.
    const int fd = create_mem_fd(name);
    if (fd != -1) {
      return fd;
    }

    log_debug_p(gc)("Falling back to searching for an accessible mount point");
  }

  return create_file_fd(name);
}

bool ZPhysicalMemoryBacking::is_initialized() const {
  return _initialized;
}

void ZPhysicalMemoryBacking::warn_available_space(size_t max_capacity) const {
  // Note that the available space on a tmpfs or a hugetlbfs filesystem
  // will be zero if no size limit was specified when it was mounted.
  if (_available == 0) {
    // No size limit set, skip check
    log_info_p(gc, init)("Available space on backing filesystem: N/A");
    return;
  }

  log_info_p(gc, init)("Available space on backing filesystem: %zuM", _available / M);

  // Warn if the filesystem doesn't currently have enough space available to hold
  // the max heap size. The max heap size will be capped if we later hit this limit
  // when trying to expand the heap.
  if (_available < max_capacity) {
    log_warning_p(gc)("***** WARNING! INCORRECT SYSTEM CONFIGURATION DETECTED! *****");
    log_warning_p(gc)("Not enough space available on the backing filesystem to hold the current max Java heap");
    log_warning_p(gc)("size (%zuM). Please adjust the size of the backing filesystem accordingly "
                      "(available", max_capacity / M);
    log_warning_p(gc)("space is currently %zuM). Continuing execution with the current filesystem "
                      "size could", _available / M);
    log_warning_p(gc)("lead to a premature OutOfMemoryError being thrown, due to failure to commit memory.");
  }
}

void ZPhysicalMemoryBacking::warn_max_map_count(size_t max_capacity) const {
  const char* const filename = ZFILENAME_PROC_MAX_MAP_COUNT;
  FILE* const file = os::fopen(filename, "r");
  if (file == nullptr) {
    // Failed to open file, skip check
    log_debug_p(gc, init)("Failed to open %s", filename);
    return;
  }

  size_t actual_max_map_count = 0;
  const int result = fscanf(file, "%zu", &actual_max_map_count);
  fclose(file);
  if (result != 1) {
    // Failed to read file, skip check
    log_debug_p(gc, init)("Failed to read %s", filename);
    return;
  }

  // The required max map count is impossible to calculate exactly since subsystems
  // other than ZGC are also creating memory mappings, and we have no control over that.
  // However, ZGC tends to create the most mappings and dominate the total count.
  // In the worst cases, ZGC will map each granule three times, i.e. once per heap view.
  // We speculate that we need another 20% to allow for non-ZGC subsystems to map memory.
  const size_t required_max_map_count = (max_capacity / ZGranuleSize) * 3 * 1.2;
  if (actual_max_map_count < required_max_map_count) {
    log_warning_p(gc)("***** WARNING! INCORRECT SYSTEM CONFIGURATION DETECTED! *****");
    log_warning_p(gc)("The system limit on number of memory mappings per process might be too low for the given");
    log_warning_p(gc)("max Java heap size (%zuM). Please adjust %s to allow for at",
                      max_capacity / M, filename);
    log_warning_p(gc)("least %zu mappings (current limit is %zu). Continuing execution "
                      "with the current", required_max_map_count, actual_max_map_count);
    log_warning_p(gc)("limit could lead to a premature OutOfMemoryError being thrown, due to failure to map memory.");
  }
}

void ZPhysicalMemoryBacking::warn_commit_limits(size_t max_capacity) const {
  // Warn if available space is too low
  warn_available_space(max_capacity);

  // Warn if max map count is too low
  warn_max_map_count(max_capacity);
}

bool ZPhysicalMemoryBacking::is_anonymous() const {
  return ZAnonymousMemoryBacking;
}

bool ZPhysicalMemoryBacking::is_tmpfs() const {
  return _filesystem == TMPFS_MAGIC;
}

bool ZPhysicalMemoryBacking::is_hugetlbfs() const {
  return _filesystem == HUGETLBFS_MAGIC;
}

bool ZPhysicalMemoryBacking::tmpfs_supports_transparent_huge_pages() const {
  // If the shmem_enabled file exists and is readable then we
  // know the kernel supports transparent huge pages for tmpfs.
  return access(ZFILENAME_SHMEM_ENABLED, R_OK) == 0;
}

char* ZPhysicalMemoryBacking::install_broken_physical_backing(zbacking_offset offset, size_t length, char* potential_new_backing_file) const {
  // Our backing file is broken. Entering degenerate mode.
  Atomic::store(&_broken_physical_backing, true);

  char* const new_backing_file = potential_new_backing_file == nullptr
      ? (char*)mmap(0, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)
      : potential_new_backing_file;

  if (new_backing_file != MAP_FAILED) {
    ZErrno err;
    fatal("Failed to mmap (%s)", err.to_string());
  }

  ZBackingRangeNode* range_node = new ZBackingRangeNode(offset, length, new_backing_file);

  ZBackingRangeNode* head = Atomic::load(&_broken_physical_backing_head);
  for (;;) {
    range_node->set_next(head);
    ZBackingRangeNode* const prev_head = Atomic::cmpxchg(&_broken_physical_backing_head, head, range_node);
    if (prev_head == head) {
      // CAS succeeded
      return new_backing_file;
    }
    head = prev_head;
  }
}

void ZPhysicalMemoryBacking::commit_failed_in_backing_file(zbacking_offset offset, size_t length) const {
  _anonymous_memory_mode.commit_in_backing_space_failed();

  // Try to get address range back.
  char* const lost_backing_addr = _physical_mapping + untype(offset);
  char* const addr = (char*)mmap(lost_backing_addr, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (addr == lost_backing_addr) {
    // Got the backing file back. Nothing more to do.
    return;
  }

  log_info_p(gc)("Backing file broken due to commit failure.");
  // Old kernels without MAP_FIXED_NOREPLACE support may have interpreted addr as a hint, reuse it.
  install_broken_physical_backing(offset, length, addr != MAP_FAILED ? addr : nullptr);
}

char* ZPhysicalMemoryBacking::remap_failed_in_backing_file(zbacking_offset offset, size_t length) const {
  _anonymous_memory_mode.set_vma_crossing_mremap_unsupported();

  // Try to get address range back.
  char* const lost_backing_addr = _physical_mapping + untype(offset);
  char* const addr = (char*)mmap(lost_backing_addr, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (addr == lost_backing_addr) {
    // Got the backing file back. Nothing more to do.
    return lost_backing_addr;
  }

  log_info_p(gc)("Backing file broken due to remap failure.");
  // Old kernels without MAP_FIXED_NOREPLACE support may have interpreted addr as a hint, reuse it.
  return install_broken_physical_backing(offset, length, addr != MAP_FAILED ? addr : nullptr);
}

bool ZPhysicalMemoryBacking::remap_failed_in_heap(zaddress_unsafe addr, size_t length) const {
  _anonymous_memory_mode.set_vma_crossing_mremap_unsupported();

  // Try to get address range back.
  char* const lost_heap_addr = (char*)untype(addr);
  char* const new_addr = (char*)mmap(lost_heap_addr, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (new_addr == lost_heap_addr) {
    // Got the backing file back. Nothing more to do.
    return true;
  }

  log_info_p(gc)("Heap address space lost.");
  if (new_addr != MAP_FAILED) {
    // Old kernels without MAP_FIXED_NOREPLACE support may have interpreted addr as a hint
    if (munmap(new_addr, length) == -1) {
      ZErrno err;
      fatal("Failed to munmap (%s)", err.to_string());
    }
  }
  return false;
}

ZErrno ZPhysicalMemoryBacking::fallocate_compat_mmap_hugetlbfs(zbacking_offset offset, size_t length, bool touch) const {
  // On hugetlbfs, mapping a file segment will fail immediately, without
  // the need to touch the mapped pages first, if there aren't enough huge
  // pages available to back the mapping.
  void* const addr = mmap(nullptr, length, PROT_READ|PROT_WRITE, MAP_SHARED, _fd, untype(offset));
  if (addr == MAP_FAILED) {
    // Failed
    return errno;
  }

  // Once mapped, the huge pages are only reserved. We need to touch them
  // to associate them with the file segment. Note that we can not punch
  // hole in file segments which only have reserved pages.
  if (touch) {
    char* const start = (char*)addr;
    char* const end = start + length;
    os::pretouch_memory(start, end, _block_size);
  }

  // Unmap again. From now on, the huge pages that were mapped are allocated
  // to this file. There's no risk of getting a SIGBUS when mapping and
  // touching these pages again.
  if (munmap(addr, length) == -1) {
    // Failed
    return errno;
  }

  // Success
  return 0;
}

static bool safe_touch_mapping(void* addr, size_t length, size_t page_size) {
  char* const start = (char*)addr;
  char* const end = start + length;

  // Touching a mapping that can't be backed by memory will generate a
  // SIGBUS. By using SafeFetch32 any SIGBUS will be safely caught and
  // handled. On tmpfs, doing a fetch (rather than a store) is enough
  // to cause backing pages to be allocated (there's no zero-page to
  // worry about).
  for (char *p = start; p < end; p += page_size) {
    if (SafeFetch32((int*)p, -1) == -1) {
      // Failed
      return false;
    }
  }

  // Success
  return true;
}

ZErrno ZPhysicalMemoryBacking::fallocate_compat_mmap_tmpfs(zbacking_offset offset, size_t length) const {
  // On tmpfs, we need to touch the mapped pages to figure out
  // if there are enough pages available to back the mapping.
  void* const addr = mmap(nullptr, length, PROT_READ|PROT_WRITE, MAP_SHARED, _fd, untype(offset));
  if (addr == MAP_FAILED) {
    // Failed
    return errno;
  }

  // Maybe madvise the mapping to use transparent huge pages
  if (os::Linux::should_madvise_shmem_thps()) {
    os::Linux::madvise_transparent_huge_pages(addr, length);
  }

  // Touch the mapping (safely) to make sure it's backed by memory
  const bool backed = safe_touch_mapping(addr, length, _block_size);

  // Unmap again. If successfully touched, the backing memory will
  // be allocated to this file. There's no risk of getting a SIGBUS
  // when mapping and touching these pages again.
  if (munmap(addr, length) == -1) {
    // Failed
    return errno;
  }

  // Success
  return backed ? 0 : ENOMEM;
}

ZErrno ZPhysicalMemoryBacking::fallocate_compat_pwrite(zbacking_offset offset, size_t length) const {
  uint8_t data = 0;

  // Allocate backing memory by writing to each block
  for (zbacking_offset pos = offset; pos < offset + length; pos += _block_size) {
    if (pwrite(_fd, &data, sizeof(data), untype(pos)) == -1) {
      // Failed
      return errno;
    }
  }

  // Success
  return 0;
}

ZErrno ZPhysicalMemoryBacking::fallocate_fill_hole_compat(zbacking_offset offset, size_t length) const {
  // fallocate(2) is only supported by tmpfs since Linux 3.5, and by hugetlbfs
  // since Linux 4.3. When fallocate(2) is not supported we emulate it using
  // mmap/munmap (for hugetlbfs and tmpfs with transparent huge pages) or pwrite
  // (for tmpfs without transparent huge pages and other filesystem types).
  if (ZLargePages::is_explicit()) {
    return fallocate_compat_mmap_hugetlbfs(offset, length, false /* touch */);
  } else if (ZLargePages::is_transparent()) {
    return fallocate_compat_mmap_tmpfs(offset, length);
  } else {
    return fallocate_compat_pwrite(offset, length);
  }
}

ZErrno ZPhysicalMemoryBacking::fallocate_fill_hole_syscall(zbacking_offset offset, size_t length) const {
  const int mode = 0; // Allocate
  const int res = ZSyscall::fallocate(_fd, mode, untype(offset), length);
  if (res == -1) {
    // Failed
    return errno;
  }

  // Success
  return 0;
}

ZErrno ZPhysicalMemoryBacking::fallocate_fill_hole(zbacking_offset offset, size_t length) const {
  // Using compat mode is more efficient when allocating space on hugetlbfs.
  // Note that allocating huge pages this way will only reserve them, and not
  // associate them with segments of the file. We must guarantee that we at
  // some point touch these segments, otherwise we can not punch hole in them.
  // Also note that we need to use compat mode when using transparent huge pages,
  // since we need to use madvise(2) on the mapping before the page is allocated.
  if (z_fallocate_supported && !ZLargePages::is_enabled()) {
     const ZErrno err = fallocate_fill_hole_syscall(offset, length);
     if (!err) {
       // Success
       return 0;
     }

     if (err != ENOSYS && err != EOPNOTSUPP) {
       // Failed
       return err;
     }

     // Not supported
     log_debug_p(gc)("Falling back to fallocate() compatibility mode");
     z_fallocate_supported = false;
  }

  return fallocate_fill_hole_compat(offset, length);
}

ZErrno ZPhysicalMemoryBacking::fallocate_punch_hole(zbacking_offset offset, size_t length) const {
  if (ZLargePages::is_explicit()) {
    // We can only punch hole in pages that have been touched. Non-touched
    // pages are only reserved, and not associated with any specific file
    // segment. We don't know which pages have been previously touched, so
    // we always touch them here to guarantee that we can punch hole.
    const ZErrno err = fallocate_compat_mmap_hugetlbfs(offset, length, true /* touch */);
    if (err) {
      // Failed
      return err;
    }
  }

  const int mode = FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE;
  if (ZSyscall::fallocate(_fd, mode, untype(offset), length) == -1) {
    // Failed
    return errno;
  }

  // Success
  return 0;
}

ZErrno ZPhysicalMemoryBacking::split_and_fallocate(bool punch_hole, zbacking_offset offset, size_t length) const {
  // Try first half
  const zbacking_offset offset0 = offset;
  const size_t length0 = align_up(length / 2, _block_size);
  const ZErrno err0 = fallocate(punch_hole, offset0, length0);
  if (err0) {
    return err0;
  }

  // Try second half
  const zbacking_offset offset1 = offset0 + length0;
  const size_t length1 = length - length0;
  const ZErrno err1 = fallocate(punch_hole, offset1, length1);
  if (err1) {
    return err1;
  }

  // Success
  return 0;
}

ZErrno ZPhysicalMemoryBacking::fallocate(bool punch_hole, zbacking_offset offset, size_t length) const {
  assert(is_aligned(untype(offset), _block_size), "Invalid offset");
  assert(is_aligned(length, _block_size), "Invalid length");

  const ZErrno err = punch_hole ? fallocate_punch_hole(offset, length) : fallocate_fill_hole(offset, length);
  if (err == EINTR && length > _block_size) {
    // Calling fallocate(2) with a large length can take a long time to
    // complete. When running profilers, such as VTune, this syscall will
    // be constantly interrupted by signals. Expanding the file in smaller
    // steps avoids this problem.
    return split_and_fallocate(punch_hole, offset, length);
  }

  return err;
}

bool ZPhysicalMemoryBacking::commit_inner(zbacking_offset offset, size_t length) const {
  log_trace(gc, heap)("Committing memory: %zuM-%zuM (%zuM)",
                      untype(offset) / M, untype(to_zbacking_offset_end(offset, length)) / M, length / M);

  if (is_anonymous()) {
    size_t covered_size = 0;
    size_t committed_size = 0;
    bool commit_failed = false;
    for_offset_length_do(offset, length, [&](char* file_addr, zbacking_offset partial_offset, size_t partial_length) {
      covered_size += partial_length;
      if (commit_failed) {
        // We stop commiting on failure.
        return;
      }

      if (_anonymous_memory_mode.commit_in_backing_space()) {
        if (mmap(file_addr, partial_length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
          ZErrno err;
          log_error_p(gc)("Failed to commit memory (%s)", err.to_string());
          commit_failed = true;
          commit_failed_in_backing_file(partial_offset, partial_length);
          return;
        }

        // Madvise transparent huge pages
        os::realign_memory((char*)file_addr, partial_length, ZGranuleSize);

        // Populate the first page fault
        *(char*)file_addr = 0;
      } else {
        // Step 1: Try grabbing the memory in a way that commits the memory to the system
        void* const res = mmap(0, partial_length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (res == MAP_FAILED) {
          // Failed
          ZErrno err;
          log_error_p(gc)("Failed to commit memory (%s)", err.to_string());
          commit_failed = true;
          return;
        }

        // Madvise transparent huge pages
        os::realign_memory((char*)res, partial_length, ZGranuleSize);

        // Step 2: Once committing has finished, slot the memory into our file mapping. This will succeed.
        void* const file_addr = _physical_mapping + untype(offset);
        if (mremap(res, partial_length, partial_length, MREMAP_MAYMOVE | MREMAP_FIXED, file_addr) == MAP_FAILED) {
          ZErrno err;
          fatal("Failed to remap memory (%s)", err.to_string());
        }

        // Populate the first page fault
        *(char*)file_addr = 0;
      }
      committed_size += partial_length;
    });
    assert(covered_size == length, "must have covered whole range");

    // TODO: Handle parital committed case.
    return !commit_failed;
  }

retry:
  const ZErrno err = fallocate(false /* punch_hole */, offset, length);
  if (err) {
    if (err == ENOSPC && !is_init_completed() && ZLargePages::is_explicit() && z_fallocate_hugetlbfs_attempts-- > 0) {
      // If we fail to allocate during initialization, due to lack of space on
      // the hugetlbfs filesystem, then we wait and retry a few times before
      // giving up. Otherwise there is a risk that running JVMs back-to-back
      // will fail, since there is a delay between process termination and the
      // huge pages owned by that process being returned to the huge page pool
      // and made available for new allocations.
      log_debug_p(gc, init)("Failed to commit memory (%s), retrying", err.to_string());

      // Wait and retry in one second, in the hope that huge pages will be
      // available by then.
      sleep(1);
      goto retry;
    }

    // Failed
    log_error_p(gc)("Failed to commit memory (%s)", err.to_string());
    return false;
  }

  // Success
  return true;
}

size_t ZPhysicalMemoryBacking::commit_numa_preferred(zbacking_offset offset, size_t length, uint32_t numa_id) const {
  // Setup NUMA policy to allocate memory from a preferred node
  os::Linux::numa_set_preferred((int)numa_id);

  const size_t committed = commit_default(offset, length);

  // Restore NUMA policy
  os::Linux::numa_set_preferred(-1);

  return committed;
}

size_t ZPhysicalMemoryBacking::commit_default(zbacking_offset offset, size_t length) const {
  // Try to commit the whole region
  if (commit_inner(offset, length)) {
    // Success
    return length;
  }

  // Failed, try to commit as much as possible
  zbacking_offset start = offset;
  zbacking_offset_end end = to_zbacking_offset_end(offset, length);

  for (;;) {
    length = align_down((end - start) / 2, ZGranuleSize);
    if (length < ZGranuleSize) {
      // Done, don't commit more
      return start - offset;
    }

    if (commit_inner(start, length)) {
      // Success, try commit more
      start += length;
    } else {
      // Failed, try commit less
      end -= length;
    }
  }
}

size_t ZPhysicalMemoryBacking::commit(zbacking_offset offset, size_t length, uint32_t numa_id) const {
  if (ZNUMA::is_enabled() && !ZLargePages::is_explicit()) {
    // The memory is required to be preferred at the time it is paged in. As a
    // consequence we must prefer the memory when committing non-large pages.
    return commit_numa_preferred(offset, length, numa_id);
  }

  return commit_default(offset, length);
}

size_t ZPhysicalMemoryBacking::uncommit(zbacking_offset offset, size_t length) const {
  log_trace(gc, heap)("Uncommitting memory: %zuM-%zuM (%zuM)",
                      untype(offset) / M, untype(to_zbacking_offset_end(offset, length)) / M, length / M);
  if (is_anonymous()) {
    size_t covered_size = 0;
    size_t uncommitted_size = 0;
    bool uncommit_failed = false;
    for_offset_length_do(offset, length, [&](char* file_addr, zbacking_offset partial_offset, size_t partial_length) {
      covered_size += partial_length;
      if (uncommit_failed) {
        // We stop uncommiting on failure.
        return;
      }

      if (_anonymous_memory_mode.use_madv_free()) {
        // Uncommitting with MADV_FREE should be a bit cheaper then synchronously unmapping
        if (mprotect(file_addr, partial_length, PROT_NONE) != 0) {
          ZErrno err;
          fatal("Failed to protect memory (%s) " PTR_FORMAT " PROT_NONE", err.to_string(), p2i(file_addr));
        }
        if (madvise(file_addr, partial_length, MADV_FREE) != 0) {
          uncommit_failed = true;
          return;
        }
      } else {
        if (mmap(file_addr, partial_length, PROT_NONE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0) == MAP_FAILED) {
          ZErrno err;
          fatal("Failed to protect memory (%s) " PTR_FORMAT " PROT_NON", err.to_string(), p2i(file_addr));
        }
      }
      uncommitted_size += partial_length;
    });
    assert(covered_size == length, "must have covered whole range");
    return uncommitted_size;
  } else {
    const ZErrno err = fallocate(true /* punch_hole */, offset, length);
    if (err) {
      log_error(gc)("Failed to uncommit memory (%s)", err.to_string());
      return 0;
    }
  }

  return length;
}

void ZPhysicalMemoryBacking::do_mremap(char* from, char* to, size_t size) const {
  const size_t granule_size = ZGranuleSize;
  assert(size % granule_size == 0, "%zu", size);

  if (_anonymous_memory_mode.remap_whole_range()) {
    if (mremap(from, size, size, MREMAP_MAYMOVE | MREMAP_DONTUNMAP | MREMAP_FIXED, to) == MAP_FAILED) {
      ZErrno err;
      log_error_p(gc)("Failed to map memory (%s) " PTR_FORMAT ", " PTR_FORMAT ", %zu", err.to_string(), p2i(from), p2i(to), size);
      if (to >= _physical_mapping && to < _physical_mapping + _size) {
        zbacking_offset offset = to_zbacking_offset((uintptr_t)(_physical_mapping - to));
        char* const new_to = remap_failed_in_backing_file(offset, size);
        // Retry with segmented mremap
        assert(!_anonymous_memory_mode.remap_whole_range(), "should not attempt again");
        do_mremap(from, new_to, size);
      } else {
        const zaddress_unsafe addr = to_zaddress_unsafe((uintptr_t)from);
        if (remap_failed_in_heap(addr, size)) {
          // Retry with segmented mremap
          assert(!_anonymous_memory_mode.remap_whole_range(), "should not attempt again");
          do_mremap(from, to, size);
        } else {
          // TODO: Unclear what to do here.
          fatal("Lost heap address");
        }
      }
      return;
    }
  } else {
    for (char* new_addr = to, * old_addr = from, *const end = to + size; new_addr != end; new_addr += granule_size, old_addr += granule_size) {
      if (mremap(old_addr, granule_size, granule_size, MREMAP_MAYMOVE | MREMAP_DONTUNMAP | MREMAP_FIXED, new_addr) == MAP_FAILED) {
        ZErrno err;
        fatal("Failed to map memory (%s) " PTR_FORMAT ", " PTR_FORMAT, err.to_string(), p2i(old_addr), p2i(new_addr));
      }
    }
  }

  const void* const res = mmap(from, size, PROT_NONE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
  if (res == MAP_FAILED) {
    ZErrno err;
    fatal("Failed to map memory (%s) " PTR_FORMAT ", " PTR_FORMAT ", %zu", err.to_string(), p2i(from), p2i(to), size);
  }
}

void ZPhysicalMemoryBacking::map(zaddress_unsafe addr, size_t size, zbacking_offset offset) const {
  if (is_anonymous()) {
    zaddress_unsafe next_addr = addr;
    for_offset_length_do(offset, size, [&](char* file_addr, zbacking_offset partial_offset, size_t partial_size) {
      do_mremap(file_addr, (char*)untype(next_addr), partial_size);
      next_addr = next_addr + partial_size;
    });
    assert(next_addr == addr + size, "must have covered whole range");
  } else {
    const void* const res = mmap((void*)untype(addr), size, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_SHARED, _fd, untype(offset));
    if (res == MAP_FAILED) {
      ZErrno err;
      fatal("Failed to map memory (%s)", err.to_string());
    }
  }
}

void ZPhysicalMemoryBacking::unmap(zaddress_unsafe addr, size_t size) const {
  if (is_anonymous()) {
    // Invalidate the whole range.
    if (mprotect((void*)untype(addr), size, PROT_NONE) != 0) {
      ZErrno err;
      fatal("Failed to protect memory (%s) " PTR_FORMAT " PROT_NONE", err.to_string(), untype(addr));
    }
  } else {
    // Note that we must keep the address space reservation intact and just detach
    // the backing memory. For this reason we map a new anonymous, non-accessible
    // and non-reserved page over the mapping instead of actually unmapping.
    const void* const res = mmap((void*)untype(addr), size, PROT_NONE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
    if (res == MAP_FAILED) {
      ZErrno err;
      fatal("Failed to map memory (%s)", err.to_string());
    }
  }
}

void ZPhysicalMemoryBacking::unmap_segment(zaddress_unsafe addr, size_t size, zbacking_offset offset) const {
  if (is_anonymous()) {
    zaddress_unsafe next_addr = addr;
    for_offset_length_do(offset, size, [&](char* file_addr, zbacking_offset partial_offset, size_t partial_size) {
      do_mremap((char*)untype(next_addr), file_addr, partial_size);
      if (mprotect(file_addr, partial_size, PROT_READ | PROT_WRITE) != 0) {
        ZErrno err;
        fatal("Failed to protect memory (%s) " PTR_FORMAT " PROT_READ | PROT_WRITE", err.to_string(), p2i(file_addr));
      }
      next_addr = next_addr + partial_size;
    });
    assert(next_addr == addr + size, "must have covered whole range");
  }
}
