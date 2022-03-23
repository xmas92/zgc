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
static size_t metadata_size[2] = {0};
static size_t heap_size[2] = {0};
static size_t heap_cap[2] = {0};

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
    log_info(gc, coops)("%s: Evaluating ExCompressedStatsHashTable",
            (id == ZGenerationId::young) ? "Young" : "Old");
    auto func = [id, seqnum](ExCompressedStatsTableConfig::Value* value){
        (*value)->evaluate(id, seqnum);
        return true;
    };
    ik_savings[(uint8_t)id] = 0;
    oak_savings[(uint8_t)id] = 0;
    metadata_size[(uint8_t)id] = 0;
    heap_size[(uint8_t)id] = Universe::heap()->used();
    heap_cap[(uint8_t)id] = Universe::heap()->max_capacity();
    if (!SafepointSynchronize::is_at_safepoint()) {
        _local_table->do_scan(Thread::current(), func);
    } else {
        _local_table->do_safepoint_scan(func);
    }
    metadata_size[(uint8_t)id] += _local_table->get_mem_size(Thread::current());
    size_t savings = ik_savings[(uint8_t)id] + oak_savings[(uint8_t)id];
    log_info(gc, coops)("%s:      Metadata used: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", metadata_size[(uint8_t)id] / M, metadata_size[(uint8_t)id]);
    log_info(gc, coops)("%s: Availiable savings: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", savings / M, savings);
    log_info(gc, coops)("%s:     Instance Klass: %ld MB (%ld)",
            (id == ZGenerationId::young) ? "Young" : "Old", ik_savings[(uint8_t)id] / M, ik_savings[(uint8_t)id]);
    if (ExCompressObjArray) {
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
    :  _min(std::numeric_limits<size_t>::max()), _max(0), _num_null(0), _distribution{0} {}

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
        if (num_fields > 0) {
            _field_data[0].at_grow(num_fields,ExCompressedFieldStatsData());
            _field_data[1].at_grow(num_fields,ExCompressedFieldStatsData());
        }
    } else {
        assert(_klass->is_objArray_klass(), "invariant");
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
        Closure(ExCompressedFieldStatsData& data, objArrayOop objarray_oop) : _data(data), _objarray_oop(objarray_oop) {}
        void do_oop(oop* obj) {
            // TODO: figure out accessors.
            oop element = HeapAccess<>::oop_load(obj);
            _data.ex_handle_field(_objarray_oop, element);
        }
        void do_oop(narrowOop* obj) {
            ShouldNotReachHere();
        }
    } closure(_field_data[(uint8_t)id].at(0), objarray_oop);
    objarray_oop->oop_iterate_range(&closure, 0, objarray_oop->length());
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
            _field_data[(uint8_t)id].at(i++).ex_handle_field(obj, field);
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
    if (oopDesc::compare(obj, field) < 0) {
       delta = pointer_delta(field, obj, MinObjAlignmentInBytes);
    } else {
       delta = pointer_delta(obj, field, MinObjAlignmentInBytes);
    }
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
    size_t prev_max = Atomic::load(&_min);
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

void ExCompressedStatsData::reset_data(ZGenerationId id) {
    _num_instances[(uint8_t)id] = 0;
    for (int i = 0; i < _field_data[(uint8_t)id].length(); ++i) {
        _field_data[(uint8_t)id].at(i).reset_data();
    }
}
void ExCompressedFieldStatsData::reset_data() {
    _min = std::numeric_limits<size_t>::max();
    _max = 0;
    _num_null = 0;
    for (size_t i = 0; i < sizeof(size_t); ++i) {
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
    if (lt.is_enabled && _num_instances[(uint8_t)id] > 0 && _seqnum[(uint8_t)id] >> 1 == seqnum) {
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
                if (field_data.get_max() > heap_cap[(uint8_t)id]) {
                    lt.print("    FarField: %10ld/%ld", field_data.get_max(), heap_cap[(uint8_t)id]);
                    auto dist = field_data.get_distribution();
                    lt.print(" Distibution: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld]",
                        field_data.get_num_null(),
                        dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], dist[6], dist[7]);
                }
            }
        } else {
            assert(_klass->is_objArray_klass(), "invariant");
            auto& field_data = _field_data[(uint8_t)id].at(0);
            auto total = field_data.get_total_num();
            oak_savings[(uint8_t)id] += total * field_data.get_min_byte_req();
            lt.print("%s(%d):[%8ld][%8ld] Class: %s. ",
                (id == ZGenerationId::young) ? "Young" : "Old",
                seqnum,
                total,
                _num_instances[(uint8_t)id],
                _klass->name()->as_C_string());
            assert(_field_data[(uint8_t)id].length() == 1, "sanity");
            if (field_data.get_max() > heap_cap[(uint8_t)id]) {
                lt.print("    FarArray: %10ld/%ld", field_data.get_max(), heap_cap[(uint8_t)id]);
                auto dist = field_data.get_distribution();
                lt.print(" Distibution: [%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld|%6ld]",
                    field_data.get_num_null(),
                    dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], dist[6], dist[7]);
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
    _max_bytes_per_reference = (log2i(_max_address_size_delta) + ExDynamicCompressedOopsMetaDataBits + 2) / BitsPerByte;
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