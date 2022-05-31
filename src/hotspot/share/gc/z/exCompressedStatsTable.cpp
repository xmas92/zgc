/*
*/

#include "precompiled.hpp"
#include "gc/z/exCompressedStatsTable.hpp"
#include "utilities/concurrentHashTable.inline.hpp"
#include "utilities/globalDefinitions.hpp"
#include "runtime/atomic.hpp"
#include "oops/oop.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
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

static size_t ik_savings[2] = {0};
static size_t oak_savings[2] = {0};
static size_t ik_redundant[2] = {0};
static size_t oak_redundant[2] = {0};
static size_t metadata_size[2] = {0};
static size_t heap_size[2] = {0};
static size_t heap_cap[2] = {0};

static volatile size_t total_size[2] = {0};

void ExCompressedStatsTable::register_mark_object(oop obj, ZGenerationId id) {
    LogTarget(Debug, gc, coops) lt_debug;
    if (lt_debug.is_enabled()) {
        volatile size_t* total_size_ptr = total_size + (uint8_t)id;
        Atomic::add(total_size_ptr, obj->size() * HeapWordSize, memory_order_relaxed);
    }
}

static volatile size_t allocating_page_store[2] = {0};


static volatile size_t verify_array_max[2] = {0};
static volatile size_t verify_array_next_index[2] = {0};
static volatile size_t verify_array_num_stores[2] = {0};

static GrowableArrayCHeap<ExVerifyStoreData,MEMFLAGS_VALUE> verify_array[2];


void ExCompressedStatsTable::verify_init() {
    size_t start_size = 4 * M;

    verify_array[0].at_grow(start_size - 1);
    verify_array[1].at_grow(start_size - 1);
    verify_array[0].clear();
    verify_array[1].clear();
    Atomic::store(&verify_array_max[0], (size_t)verify_array[0].max_length());
    Atomic::store(&verify_array_max[1], (size_t)verify_array[1].max_length());
    Atomic::store(&verify_array_next_index[0], (size_t)0);
    Atomic::store(&verify_array_next_index[1], (size_t)0);
    Atomic::store(&verify_array_num_stores[0], (size_t)0);
    Atomic::store(&verify_array_num_stores[1], (size_t)0);
    log_info(gc, coops)("Init ExVerifyAllStores");
}

void ExCompressedStatsTable::register_allocating_page_store(ZGenerationId id) {
    volatile size_t* allocating_page_store_ptr = allocating_page_store + (uint8_t)id;
    Atomic::inc(allocating_page_store_ptr, memory_order_relaxed);
}

void ExCompressedStatsTable::verify_register_store(ZGenerationId id, size_t dst, size_t value) {
    volatile size_t* verify_array_max_ptr = verify_array_max + (uint8_t)id;
    volatile size_t* verify_array_next_index_ptr = verify_array_next_index + (uint8_t)id;
    volatile size_t* verify_array_num_stores_ptr = verify_array_num_stores + (uint8_t)id;
    auto& array = verify_array[(uint8_t)id];

    size_t next_index = Atomic::fetch_and_add(verify_array_next_index_ptr, (size_t)1, memory_order_relaxed);
    size_t max = Atomic::load(verify_array_max_ptr);
    Atomic::inc(verify_array_num_stores_ptr, memory_order_relaxed);
    if (next_index < max) {
        array.at_put(next_index, ExVerifyStoreData(dst, value));
    }
}

void ExCompressedStatsTable::verify_mark_start(ZGenerationId id) {
    assert(SafepointSynchronize::is_at_safepoint(), "sanity");
    volatile size_t* verify_array_max_ptr = verify_array_max + (uint8_t)id;
    volatile size_t* verify_array_next_index_ptr = verify_array_next_index + (uint8_t)id;
    volatile size_t* verify_array_num_stores_ptr = verify_array_num_stores + (uint8_t)id;
    auto& array = verify_array[(uint8_t)id];
    size_t max = Atomic::load_acquire(verify_array_max_ptr);
    size_t num_stores = Atomic::load_acquire(verify_array_num_stores_ptr);
    size_t num_stored = MIN2(max,num_stores);
    assert(num_stored <= (size_t)array.length(), "sanity");
    array.trunc_to(num_stored);
    array.sort(ExVerifyStoreData::compare);
}
void ExCompressedStatsTable::verify_mark_end(ZGenerationId id) {
    assert(SafepointSynchronize::is_at_safepoint(), "sanity");
    volatile size_t* verify_array_max_ptr = verify_array_max + (uint8_t)id;
    volatile size_t* verify_array_next_index_ptr = verify_array_next_index + (uint8_t)id;
    volatile size_t* verify_array_num_stores_ptr = verify_array_num_stores + (uint8_t)id;
    auto& array = verify_array[(uint8_t)id];
    size_t max = Atomic::load_acquire(verify_array_max_ptr);
    size_t num_stores = Atomic::load_acquire(verify_array_num_stores_ptr);
    size_t new_max = MAX2(max,num_stores);
    Atomic::release_store(verify_array_next_index_ptr, (size_t)0);
    Atomic::release_store(verify_array_num_stores_ptr, (size_t)0);
    array.clear();
    if (new_max > 0) {
        array.at_grow(new_max - 1);
        new_max = array.max_length();
        array.at_grow(new_max - 1);
    }
    Atomic::release_store(verify_array_max_ptr, new_max);
    size_t array_size = array.data_size_in_bytes();
    if (num_stores > max) {
        log_info(gc, coops)("%s: Verify Missed %ld stores",
                (id == ZGenerationId::young) ? "Young" : "Old", num_stores - max);
    }
    log_info(gc, coops)("%s:  Verify Array Size: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", array_size / M, array_size);
}

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
void ExCompressedStatsTable::evaluate_table(ZGenerationId id, uint32_t seqnum) {
    if (!ExUseDynamicCompressedOops)
        return;
    assert(_local_table != NULL, "Should have been created");
    log_info(gc, coops)("%s(%d): Evaluating ExCompressedStatsHashTable",
            (id == ZGenerationId::young) ? "Young" : "Old", seqnum);
    if (ExVerifyAllStores && seqnum == 0) {
        volatile size_t* allocating_page_store_ptr = allocating_page_store + (uint8_t)id;
        size_t allocating_page_store = Atomic::load_acquire(allocating_page_store_ptr);
        log_info(gc, coops)("%s(%d): allocating_page_store: %ld",
                (id == ZGenerationId::young) ? "Young" : "Old", seqnum,
                allocating_page_store);
        Atomic::release_store(allocating_page_store_ptr, (size_t)0);
    }
    if (ExVerifyAllStores) {
        auto& array = verify_array[(uint8_t)id];
        ExCompressedFieldStatsData total_store_data;
        auto dist = total_store_data.get_distribution();
        for (int i = 0; i < array.length(); ++i) {
            auto& store = array.at(i);
            size_t delta = (store._dst < store._value) ?
                store._value - store._dst :
                store._dst - store._value;
            total_store_data.ex_handle_delta(delta);
        }
        log_info(gc, coops)
        ("%s(%d):   Total Stores: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld](%ld,%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", seqnum,
            total_store_data.get_num_null(),
            dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], dist[6], dist[7],
            total_store_data.get_min(), total_store_data.get_max());
    }
    auto func = [id, seqnum](ExCompressedStatsTableConfig::Value* value){
        (*value)->evaluate(id, seqnum);
        return true;
    };
    ik_savings[(uint8_t)id] = 0;
    oak_savings[(uint8_t)id] = 0;
    ik_redundant[(uint8_t)id] = 0;
    oak_redundant[(uint8_t)id] = 0;
    metadata_size[(uint8_t)id] = 0;
    heap_size[(uint8_t)id] = Universe::heap()->used();
    heap_cap[(uint8_t)id] = Universe::heap()->max_capacity();
    volatile size_t* total_size_ptr = total_size + (uint8_t)id;
    size_t total_size = Atomic::load_acquire(total_size_ptr);
    if (!SafepointSynchronize::is_at_safepoint()) {
        _local_table->do_scan(Thread::current(), func);
    } else {
        _local_table->do_safepoint_scan(func);
    }
    Atomic::release_store(total_size_ptr, size_t(0));
    metadata_size[(uint8_t)id] += _local_table->get_mem_size(Thread::current());
    size_t savings = ik_savings[(uint8_t)id] + oak_savings[(uint8_t)id];
    size_t redundant = ik_redundant[(uint8_t)id] + oak_redundant[(uint8_t)id];
    log_debug(gc, coops)("%s:    Generation Size: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", total_size / M, total_size);
    log_info(gc, coops)("%s:    Redundant Bytes: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", redundant / M, redundant);
    if (ExCompressObjArray) {
    log_info(gc, coops)("%s:     Instance Klass: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", ik_redundant[(uint8_t)id] / M, ik_redundant[(uint8_t)id]);
    log_info(gc, coops)("%s:     ObjArray Klass: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", oak_redundant[(uint8_t)id] / M, oak_redundant[(uint8_t)id]);
    }
    log_info(gc, coops)("%s:      Metadata used: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", metadata_size[(uint8_t)id] / M, metadata_size[(uint8_t)id]);
    log_info(gc, coops)("%s: Availiable savings: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", savings / M, savings);
    if (ExCompressObjArray) {
    log_info(gc, coops)("%s:     Instance Klass: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", ik_savings[(uint8_t)id] / M, ik_savings[(uint8_t)id]);
    log_info(gc, coops)("%s:     ObjArray Klass: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", oak_savings[(uint8_t)id] / M, oak_savings[(uint8_t)id]);
    }
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
    assert(_local_table != NULL, "ExCompressionHeuristics not initialized");
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

ExCompressedFieldStatsData::ExCompressedFieldStatsData()
    :  _min(std::numeric_limits<size_t>::max()), _max(0), _num_null(0), _max_num_worse_distribution(0), _distribution{0} {}

ExCompressedStatsData::ExCompressedStatsData(Klass* klass)
    : _klass(klass), _num_instances{0}, _seqnum{0}, _field_data{}   {

    if (_klass->is_instance_klass()) {
        InstanceKlass* ik = InstanceKlass::cast(_klass);
        size_t num_fields = 0;
        for (AllFieldStream fs(ik); !fs.done(); fs.next()) {
            if (!fs.access_flags().is_static() &&
                    is_reference_type(fs.field_descriptor().field_type())) {
                num_fields++;
            }
        }
        if (ExVerifyAllStores) {
            num_fields *= 2;
        }
        if (num_fields > 0) {
            _field_data[0].at_grow(num_fields-1,ExCompressedFieldStatsData());
            _field_data[1].at_grow(num_fields-1,ExCompressedFieldStatsData());
        }
    } else {
        assert(_klass->is_objArray_klass(), "invariant");
        _field_data[0].append(ExCompressedFieldStatsData());
        _field_data[1].append(ExCompressedFieldStatsData());
        // For per objArray max element delta
        _field_data[0].append(ExCompressedFieldStatsData());
        _field_data[1].append(ExCompressedFieldStatsData());
    }
}

ExCompressedStatsData::~ExCompressedStatsData() {

}

void ExCompressedStatsData::ex_handle_object_instanceklass(ZGenerationId id, uint32_t seqnum, oop obj, InstanceKlass* ik) {
    // TODO add assert check that ik is in obj superklass hiearchy
    assert(_klass == ik, "sanity");
    ex_handle_seqnum(id, seqnum);
    ex_handle_object_common(id, obj, ik);
}

inline size_t pointer_delta_unord(oop o1, oop o2) {
    if (oopDesc::compare(o1, o2) < 0) {
       return pointer_delta(o2, o1, MinObjAlignmentInBytes);
    } else {
       return pointer_delta(o1, o2, MinObjAlignmentInBytes);
    }
}

void ExCompressedStatsData::ex_handle_object_objarrayklass(ZGenerationId id, uint32_t seqnum, oop obj, const ObjArrayKlass* oak) {
    // TODO add assert check that ik is in obj superklass hiearchy
    assert(_klass == oak, "sanity");
    ex_handle_seqnum(id, seqnum);
    // Bump instance count.
    Atomic::inc(_num_instances + (uint8_t)id, memory_order_relaxed);
    objArrayOop objarray_oop = objArrayOop(obj);
    struct Closure : public OopClosure {
        ExCompressedFieldStatsData& _data;
        objArrayOop _objarray_oop;
        oop _max_element;
        oop _min_element;
        Closure(ExCompressedFieldStatsData& data, objArrayOop objarray_oop)
        : _data(data), _objarray_oop(objarray_oop), _max_element((oop)zaddress::null), _min_element((oop)zaddress::null) {}
        void do_oop(oop* obj) {
            // TODO: figure out accessors.
            oop element = HeapAccess<>::oop_load(obj);
            if (_max_element == (oop)zaddress::null)
                _max_element = element;
            if (_min_element == (oop)zaddress::null)
                _min_element = element;
            if (element != (oop)zaddress::null) {
                if (oopDesc::compare(element, _min_element) < 0) {
                    _min_element = element;
                } else if (oopDesc::compare(_max_element, element) < 0) {
                    _max_element = element;
                }
            }
            assert(oopDesc::compare(_min_element, _max_element) <= 0, "invariant");
            _data.ex_handle_field(_objarray_oop, element);
        }
        void do_oop(narrowOop* obj) {
            ShouldNotReachHere();
        }
    } closure(_field_data[(uint8_t)id].at(0), objarray_oop);
    objarray_oop->oop_iterate_range(&closure, 0, objarray_oop->length());
    _field_data[(uint8_t)id].at(1).ex_handle_field(closure._min_element, closure._max_element);
}

void ExCompressedStatsData::ex_handle_object(ZGenerationId id, uint32_t seqnum, oop obj) {
    assert(_klass == obj->klass(), "must be same class");
    if (_klass->is_instance_klass()) {
        InstanceKlass* ik = InstanceKlass::cast(_klass);
        ex_handle_object_instanceklass(id, seqnum, obj, ik);
    }
}

void ExCompressedStatsData::ex_handle_object_common(ZGenerationId id, oop obj, InstanceKlass* ik) {
    // Bump instance count.
    Atomic::inc(_num_instances + (uint8_t)id, memory_order_relaxed);

    // Handle local fields
    int i = 0;
    for (AllFieldStream fs(ik); !fs.done(); fs.next()) {
        if (!fs.access_flags().is_static() &&
                is_reference_type(fs.field_descriptor().field_type())) {
            oop field = obj->obj_field(fs.offset());
            _field_data[(uint8_t)id].at(i).ex_handle_field(obj, field);
            if (ExVerifyAllStores) {
                auto len = _field_data[(uint8_t)id].length();
                assert(len%2 == 0, "sanity");
                assert(len/2 + i < len, "sanity");
                oop* field_addr = obj->field_addr<oop>(fs.offset());
                _field_data[(uint8_t)id].at(len/2 + i).ex_verify_field(obj, field_addr, id);
            }
            ++i;
        }
    }
}

size_t ExCompressedFieldStatsData::get_total_num() {
    size_t total = 0;
    total += get_num_null();
    for (size_t i = 0; i < sizeof(size_t); ++i) {
        total += get_distribution()[i];
    }
    return total;
}

size_t ExCompressedFieldStatsData::get_min_byte_req() {
    size_t byte = sizeof(size_t);
    do {
        --byte;
    } while (byte > 0 && _distribution[byte] == 0);
    return byte + 1;
}

void ExCompressedFieldStatsData::ex_handle_field(oop obj, oop field) {
    assert(oopDesc::is_oop_or_null(field), "sanity");
    size_t delta;
    if (field == NULL) {
        Atomic::inc(&_num_null);
        return;
    }
    delta = pointer_delta_unord(obj, field);
    ex_handle_delta(delta);
}

void ExCompressedFieldStatsData::ex_handle_delta(size_t delta) {
     assert(delta < (std::numeric_limits<size_t>::max() >> 1), "should be true");
    // need one bit for direction
    delta = delta << 1;

    // store min and max deltas
    size_t prev_min = Atomic::load(&_min);
    if (prev_min > delta) {
        size_t load_min = prev_min;
        do {
            prev_min = load_min;
            load_min = Atomic::cmpxchg(&_min, prev_min, delta);
            if (load_min <= delta)
                break;
        } while (prev_min != load_min);
    }
    size_t prev_max = Atomic::load(&_max);
    if (prev_max < delta) {
        size_t load_max = prev_max;
        do {
            prev_max = load_max;
            load_max = Atomic::cmpxchg(&_max, prev_max, delta);
            if (load_max >= delta)
                break;
        } while (prev_min != load_max);
    }
    int i = -1;
    do {
        delta >>= 8;
        ++i;
    } while (delta != 0);
    assert(i >= 0 && i < 8, "sanity");
    Atomic::inc(_distribution + i);
}

void ExCompressedFieldStatsData::ex_verify_field(oop obj, oop* field_addr, ZGenerationId id) {
    auto& array = verify_array[(uint8_t)id];
    bool found = false;
    int index = array.find_sorted<size_t,ExVerifyStoreData::compare>((size_t)field_addr, found);
    if (!found) {
        return;
    }
    auto& store = array.at(index);
    while(ExVerifyStoreData::compare((size_t)field_addr, store) == 0) {
        ex_handle_field(obj, to_oop((zaddress)store._value));
        ++index;
        if (index >= array.length()) {
            break;
        }
        store = array.at(index);
    }


}

void ExCompressedStatsData::reset_data(ZGenerationId id) {
    _num_instances[(uint8_t)id] = 0;
    for (int i = 0; i < _field_data[(uint8_t)id].length(); ++i) {
        _field_data[(uint8_t)id].at(i).reset_data();
    }
}

size_t ExCompressedFieldStatsData::get_num_worse() {
    const auto log_sizeof_size_t = log2i_exact(sizeof(size_t));
    return _max_num_worse_distribution >> log_sizeof_size_t;
}

size_t ExCompressedFieldStatsData::get_num_worse_byte_req() {
    const auto log_sizeof_size_t = log2i_exact(sizeof(size_t));
    const auto byte_bitmask = ~(~size_t(0) << log_sizeof_size_t);
    return _max_num_worse_distribution & byte_bitmask;
}

void ExCompressedFieldStatsData::reset_data() {
    _min = std::numeric_limits<size_t>::max();
    _max = 0;
    _num_null = 0;
    const auto log_sizeof_size_t = log2i_exact(sizeof(size_t));
    const auto byte_bitmask = ~(~size_t(0) << log_sizeof_size_t);
    const auto last_max_num_worse_distribution = _max_num_worse_distribution >> log_sizeof_size_t;
    const auto last_max_num_worse_distribution_byte = _max_num_worse_distribution & byte_bitmask;
    assert(last_max_num_worse_distribution_byte < sizeof(size_t), "sanity");
    for (size_t i = 0; i < last_max_num_worse_distribution_byte; ++i) {
        _distribution[i] = 0;
    }
    for (size_t i = last_max_num_worse_distribution_byte; i < sizeof(size_t); ++i) {
        if (_distribution[i] != 0 &&
            (_distribution[i] > last_max_num_worse_distribution ||
             i > last_max_num_worse_distribution_byte)) {
            _max_num_worse_distribution = (_distribution[i] << log_sizeof_size_t) & i;
        }
        _distribution[i] = 0;
    }
}
void ExCompressedStatsData::ex_handle_seqnum(ZGenerationId id, uint32_t seqnum) {
    auto* seqnum_ptr = _seqnum + (uint8_t)id;
    uint64_t cur_seqnum = Atomic::load_acquire(seqnum_ptr);
    uint64_t new_seqnum = (uint64_t)seqnum << 1;
    // _seqnum is either:
    //         * previous seqnum (must lock and reset)
    //         * same as cur_seqnum (free to use)
    //         * cur_seqnum + 1 (locked and reseting)
    while (cur_seqnum != new_seqnum) {
        if (cur_seqnum % 1 == 0) {
            // No one has locked
            size_t loaded_seqnum = Atomic::cmpxchg(seqnum_ptr, cur_seqnum, new_seqnum+1);
            // Why reload, cmpxchg does not return value?
            if (cur_seqnum == loaded_seqnum) {
                // Aquired lock
                reset_data(id);
                // Release lock
                Atomic::release_store(seqnum_ptr, new_seqnum);
                break;
            }
            cur_seqnum = loaded_seqnum;
            // assert(cur_seqnum % 1 == 1 || cur_seqnum == new_seqnum, "should be locked or set");
            // assert sometimes fail, example:
            // (gdb) info locals
            // loaded_seqnum = 6
            // seqnum_ptr = 0x7f462cb24eb0
            // cur_seqnum = 6
            // new_seqnum = 8
            // (gdb) p *seqnum_ptr
            // $1 = 6
        } else {
            // Locked, reload and try again.
            cur_seqnum = Atomic::load_acquire(seqnum_ptr);
        }
    }
}

void ExCompressedStatsData::evaluate(ZGenerationId id, uint32_t seqnum) {
    metadata_size[(uint8_t)id] += sizeof(*this) + 2 * _field_data[(uint8_t)id].data_size_in_bytes();
    if (_klass->is_instance_klass()) {
        metadata_size[(uint8_t)id] += sizeof(ExCompressionGains);
    }
    LogTarget(Info, gc, coops) lt;
    LogTarget(Debug, gc, coops) lt_debug;
    if (lt.is_enabled() && _num_instances[(uint8_t)id] > 0 && _seqnum[(uint8_t)id] >> 1 == seqnum) {
        ResourceMark rm;
        if (_klass->is_instance_klass()) {
            auto compression_gains = InstanceKlass::cast(_klass)->compression_gains();
            ik_savings[(uint8_t)id] += compression_gains->get_min_comperssion() * _num_instances[(uint8_t)id];
            lt.print("%s(%d):[%3d][%3d][%8ld] Class: %s. ",
                (id == ZGenerationId::young) ? "Young" : "Old",
                seqnum,
                compression_gains->get_min_comperssion(),
                compression_gains->get_max_comperssion(),
                _num_instances[(uint8_t)id],
                _klass->name()->as_C_string());
            for (int i = 0; i < _field_data[(uint8_t)id].length(); ++i) {
                auto& field_data = _field_data[(uint8_t)id].at(i);
                if (!ExVerifyAllStores || i < _field_data[(uint8_t)id].length() / 2)
                ik_redundant[(uint8_t)id] += (sizeof(size_t) - field_data.get_min_byte_req()) * _num_instances[(uint8_t)id];
                auto log_text = (i >= _field_data[(uint8_t)id].length() / 2 && ExVerifyAllStores) ?
                                " Store Dist" :
                                "Distibution";
                if (field_data.get_max() > heap_cap[(uint8_t)id]) {
                    lt.print("    FarField: %10ld/%ld", field_data.get_max(), heap_cap[(uint8_t)id]);
                    auto dist = field_data.get_distribution();
                    lt.print(" %s: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld](%ld,%ld)",
                        log_text,
                        field_data.get_num_null(),
                        dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], dist[6], dist[7],
                        field_data.get_min(), field_data.get_max());
                } else if (lt_debug.is_enabled()) {
                    auto dist = field_data.get_distribution();
                    lt_debug.print(" %s: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld](%ld,%ld)",
                        log_text,
                        field_data.get_num_null(),
                        dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], dist[6], dist[7],
                        field_data.get_min(), field_data.get_max());
                }
            }
        } else {
            assert(_klass->is_objArray_klass(), "invariant");
            auto& field_data = _field_data[(uint8_t)id].at(0);
            auto& max_field_data = _field_data[(uint8_t)id].at(1);
            auto total = field_data.get_total_num();
            oak_savings[(uint8_t)id] += total * (sizeof(size_t) - ExCompressionHeuristics::get_max_bytes_per_reference());
            oak_redundant[(uint8_t)id] += total * (sizeof(size_t) - field_data.get_min_byte_req());
            lt.print("%s(%d):[%8ld][%8ld] Class: %s. ",
                (id == ZGenerationId::young) ? "Young" : "Old",
                seqnum,
                total,
                _num_instances[(uint8_t)id],
                _klass->name()->as_C_string());
            assert(_field_data[(uint8_t)id].length() == 2, "sanity");
            if (field_data.get_max() > heap_cap[(uint8_t)id]) {
                lt.print("    FarArray: %10ld/%ld", field_data.get_max(), heap_cap[(uint8_t)id]);
                auto dist = field_data.get_distribution();
                auto max_dist = max_field_data.get_distribution();
                lt.print(" Distibution: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld](%ld,%ld)",
                    field_data.get_num_null(),
                    dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], dist[6], dist[7],
                    field_data.get_min(), field_data.get_max());
                lt.print("ElementDelta: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld](%ld,%ld)",
                    max_field_data.get_num_null(),
                    max_dist[0], max_dist[1], max_dist[2], max_dist[3], max_dist[4], max_dist[5], max_dist[6], max_dist[7],
                    max_field_data.get_min(), max_field_data.get_max());
            } else if (lt_debug.is_enabled()) {
                auto dist = field_data.get_distribution();
                auto max_dist = max_field_data.get_distribution();
                lt_debug.print(" Distibution: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld](%ld,%ld)",
                    field_data.get_num_null(),
                    dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], dist[6], dist[7],
                    field_data.get_min(), field_data.get_max());
                lt_debug.print("ElementDelta: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld](%ld,%ld)",
                    max_field_data.get_num_null(),
                    max_dist[0], max_dist[1], max_dist[2], max_dist[3], max_dist[4], max_dist[5], max_dist[6], max_dist[7],
                    max_field_data.get_min(), max_field_data.get_max());
            }
        }
    }
}

static size_t _max_address_size = 0;
static size_t _max_address_size_delta = 0;
static size_t _max_bytes_per_reference = 0;

void ExCompressionHeuristics::init_max_address_size() {
    _max_address_size = Universe::heap()->max_capacity();
    _max_address_size *= ZVirtualToPhysicalRatio;
    _max_address_size_delta = _max_address_size;
    _max_address_size_delta >>= LogMinObjAlignmentInBytes;
    _max_bytes_per_reference = (log2i(_max_address_size_delta) + ExDynamicCompressedOopsMetaDataBits + 2 + BitsPerByte - 1) / BitsPerByte;
    _max_bytes_per_reference = MIN2(_max_bytes_per_reference, size_t(8));
}

size_t ExCompressionHeuristics::get_max_address_size() {
    return _max_address_size;
}
size_t ExCompressionHeuristics::get_max_address_size_delta() {
    return _max_address_size_delta;
}
size_t ExCompressionHeuristics::get_max_bytes_per_reference() {
    return _max_bytes_per_reference;
}