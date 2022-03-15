/*
*/

#include "precompiled.hpp"
#include "gc/z/exCompressedStatsTable.hpp"
#include "utilities/concurrentHashTable.inline.hpp"
#include "utilities/globalDefinitions.hpp"
#include "runtime/atomic.hpp"
#include <limits>



class ExCompressedStatsTableConfig : public StackObj {
 private:
 public:
  using Value = ExCompressedStatsData*;

  static uintx get_hash(Value const& value, bool* is_dead) {
    *is_dead = value == NULL;
    if (*is_dead) {
        return 0;
    }
    return value->klass()->identity_hash();
  }

  static void* allocate_node(void* context, size_t size, Value const& value) {
    ExCompressedStatsTable::item_added();
    return AllocateHeap(size, MEMFLAGS_VALUE);
  }

  static void free_node(void* context, void* memory, Value const& value) {
    // TODO: Destruct Value
    delete value;
    FreeHeap(memory);
    ExCompressedStatsTable::item_removed();
  }
};
using ExCompressedStatsHashTable = ConcurrentHashTable<ExCompressedStatsTableConfig, MEMFLAGS_VALUE>;
static ExCompressedStatsHashTable* _local_table = NULL;


static volatile size_t _count = 0;
void ExCompressedStatsTable::item_added() {
    Atomic::inc(&_count);
}
void ExCompressedStatsTable::item_removed() {
    Atomic::dec(&_count);
}
void ExCompressedStatsTable::create_table() {
    assert(_local_table == NULL, "Should only create ExCompressedStatsHashTable once");
    log_info(gc, coops)("Creating ExCompressedStatsHashTable");
    _local_table = new ExCompressedStatsHashTable();
}

struct Lookup {
    Klass* _klass;
    uintx _hash;
    Lookup(Klass* klass) : _klass(klass), _hash(klass->identity_hash()) {}
    uintx get_hash() const {
        return _hash;
    }

    bool equals(ExCompressedStatsTableConfig::Value* value, bool* is_dead) const {
        assert(value != NULL, "sanity");
        return _klass == (*value)->klass();
    }
    void operator()(ExCompressedStatsTableConfig::Value*) const {
        // Deleter
        // Todo: Maybe log deletion.
        // Maybe seperate into different struct with a cause, if
        // not only klass unloading triggers this. Maybe we give
        // up on compression on some classes
    }
};
ExCompressedStatsData* ExCompressedStatsTable::new_entry(Klass* klass) {
    auto thread = Thread::current();
    ExCompressedStatsTableConfig::Value value = new ExCompressedStatsData(klass);
    Lookup lookup(klass);
    if (!_local_table->insert(Thread::current(), lookup, value)) {
        assert(false, "Should not insert klass twice");
    }
    return value;
}

void ExCompressedStatsTable::unload_klass(Klass* klass) {
    Lookup lookup(klass);
    if (!_local_table->remove(Thread::current(), lookup, lookup)) {
        // Note this might happen if we add another reason for unloading,
        // as there will then be a race for removing the class
        assert(false, "Should not unload klass twice");

    }
}

ExCompressedStatsData::ExCompressedStatsData(Klass* klass)
    : _klass(klass), _min{std::numeric_limits<std::remove_extent<decltype(_min)>::type>::max()}, _max{0}, _num_instances{0}, _distribution{0}  {
        static_assert(std::numeric_limits<std::remove_extent<decltype(_max)>::type>::min() == 0, "Use unsigned type");
}

ExCompressedStatsData::~ExCompressedStatsData() {

}
