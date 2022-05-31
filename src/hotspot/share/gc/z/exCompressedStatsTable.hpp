/*
*/

#ifndef SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP
#define SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP

#include "memory/allocation.hpp"
#include "gc/z/zGenerationId.hpp"
#include "oops/instanceKlass.hpp"
#include "gc/z/zAddress.inline.hpp"


auto const MEMFLAGS_VALUE = mtInternal;
class ExCompressedStatsData;

class ExVerifyStoreData : public CHeapObj<MEMFLAGS_VALUE> {
public:
    size_t _dst;
    size_t _value;
    ExVerifyStoreData() : _dst(0), _value(0) {}
    ExVerifyStoreData(size_t dst, size_t value) : _dst(dst), _value(value) {}

    static int compare(ExVerifyStoreData* a, ExVerifyStoreData* b) {
        if (a->_dst < b->_dst) return -1;
        if (a->_dst > b->_dst) return 1;
        return 0;
    }
    static int compare(const size_t& a, const ExVerifyStoreData& b) {
        if (a < b._dst) return -1;
        if (a > b._dst) return 1;
        return 0;
    }
};

class ExCompressedStatsTable : public CHeapObj<MEMFLAGS_VALUE> {
    friend class ExCompressedStatsTableConfig;
    friend class ExCompressedStatsData;

    static void item_added();
    static void item_removed();
public:
    static void create_table();

    static ExCompressedStatsData* new_entry(Klass* klass);

    static void unload_klass(Klass* klass);

    static void evaluate_table(ZGenerationId id, uint32_t seqnum);

    static void register_mark_object(oop obj, ZGenerationId id);

    static void register_allocating_page_store(ZGenerationId id);
    static void verify_mark_start(ZGenerationId id);
    static void verify_mark_end(ZGenerationId id);
    static void verify_register_store(ZGenerationId id, size_t dst, size_t value);
    static void verify_init();
};

class ExCompressedFieldStatsData : public CHeapObj<MEMFLAGS_VALUE> {
    volatile size_t _min, _max, _num_null, _max_num_worse_distribution;
    volatile size_t _distribution[sizeof(size_t)];
public:
    ExCompressedFieldStatsData();

    size_t get_max() { return _max; }
    size_t get_min() { return _min; }
    size_t get_num_null() { return _num_null; }
    volatile size_t* get_distribution() { return _distribution; }

    size_t get_num_worse();
    size_t get_num_worse_byte_req();

    size_t get_total_num();
    size_t get_min_byte_req();
    void ex_handle_field(oop obj, oop field);
    void reset_data();

    void ex_verify_field(oop obj, oop* field_addr, ZGenerationId id);

    void ex_handle_delta(size_t delta);
};

class ExCompressedStatsData : public CHeapObj<MEMFLAGS_VALUE> {
public:
enum CompressionStatus {
    CompressEvaluate,
    CompressNever,
    CompressLikely,
    CompressPending,
    CompressComplete,
    // Remove from compression table after marking
    CompressAbort,
};

private:
    Klass* _klass;
    volatile size_t _num_instances[2];
    volatile CompressionStatus _status[2];
    volatile uint64_t _seqnum[2];
    GrowableArrayCHeap<ExCompressedFieldStatsData, MEMFLAGS_VALUE> _field_data[2];
public:
    ExCompressedStatsData(Klass* klass);
    ~ExCompressedStatsData();

    Klass* klass() const { return _klass; }

    void ex_handle_object(ZGenerationId id, uint32_t seqnum, oop obj);
    void ex_handle_object_instanceklass(ZGenerationId id, uint32_t seqnum, oop obj, InstanceKlass* ik);
    void ex_handle_object_objarrayklass(ZGenerationId id, uint32_t seqnum, oop obj, const ObjArrayKlass* oak);

    void evaluate(ZGenerationId id, uint32_t seqnum);

    void initial_compression_status(CompressionStatus status) {
        _status[0] = _status[1] = status;
    }


    // void ex_verify_object(ZGenerationId id, oop obj, zaddress field, zaddress store);
    // void ex_verify_object_field(ZGenerationId id, oop obj, zaddress field, zaddress store, InstanceKlass* ik);
    // void ex_verify_object_element(ZGenerationId id, oop obj, zaddress element, zaddress store, ObjArrayKlass* oak);

private:
    void ex_handle_object_common(ZGenerationId id, oop obj, InstanceKlass* ik);
    void ex_handle_seqnum(ZGenerationId id, uint32_t seqnum);
    void reset_data(ZGenerationId id);
};

class ExCompressionGains ;

class ExCompressionHeuristics : StackObj {
    friend class ExCompressionGains;
private:
    static inline ExCompressedStatsData::CompressionStatus log_rejected(InstanceKlass* ik, const char* reason);
    static inline ExCompressedStatsData::CompressionStatus log_selected(ExCompressedStatsData::CompressionStatus status, InstanceKlass* ik, const char* reason);
    static inline ExCompressedStatsData::CompressionStatus should_consider_compression_of_loaded_instance_klass(InstanceKlass* ik, ExCompressionGains* compression_gains);

    static void init_max_address_size();
public:
    static inline void handle_loaded_instance_class(InstanceKlass* ik, ExCompressionGains*& compression_gains);
    static inline void handle_create_objarray_class(ObjArrayKlass* oak);
    static inline void handle_mark_object(oop obj, ZGenerationId id, uint32_t seqnum);
    static inline void initialize();

    static inline void ex_verify_store(zaddress src, zpointer* dst);

    static size_t get_max_address_size();
    static size_t get_max_address_size_delta();
    static size_t get_max_bytes_per_reference();
};

class ExCompressionGains : public CHeapObj<MEMFLAGS_VALUE> {
    uint32_t _compression[sizeof(size_t)];
    uint32_t _min_compression;
    uint32_t _max_compression;
    uint32_t _uncompressed_size;
    u2     _num_reference_fields;
public:
    void set_compression(const uint32_t* compression) {
        for (size_t i = 0; i < sizeof(size_t) - 1; ++i) {
            assert(compression[i] >= compression[i+1], "sanity");
        }
        for (size_t i = 0; i < sizeof(size_t); ++i) {
            _compression[i] = compression[i];
        }
    }
    void set_num_reference_fields(u2 num_reference_fields) {
        _num_reference_fields = num_reference_fields;
    }
    void set_uncompressed_size(uint32_t uncompressed_size) {
        _uncompressed_size = uncompressed_size;
    }
    uint32_t get_uncompressed_size() {
        return _uncompressed_size;
    }
    uint32_t get_min_comperssion() {
       return _compression[ExCompressionHeuristics::get_max_bytes_per_reference() - 1];
    }
    uint32_t get_max_comperssion() {
       return _compression[0];
    }
    uint32_t get_comperssion(int reference_bytes) {
        assert(reference_bytes >= 1 && (size_t)reference_bytes <= sizeof(size_t), "must be valid number of bytes");
        return _compression[reference_bytes-1];
    }
    u2 get_num_reference_fields() {
        return _num_reference_fields;
    }
};


#endif // SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP