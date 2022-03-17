/*
*/

#ifndef SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP
#define SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP

#include "memory/allocation.hpp"
#include "gc/z/zGenerationId.hpp"
#include "oops/instanceKlass.hpp"


auto const MEMFLAGS_VALUE = mtInternal;
class ExCompressedStatsData;

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
};

class ExCompressedFieldStatsData : public CHeapObj<MEMFLAGS_VALUE> {
    volatile size_t _min, _max, _num_null;
    volatile size_t _distribution[sizeof(size_t)];
public:
    ExCompressedFieldStatsData();

    size_t get_max() { return _max; }
    size_t get_min() { return _min; }
    size_t get_num_null() { return _num_null; }
    volatile size_t* get_distribution() { return _distribution; }

    void ex_handle_field(oop obj, oop field);
    void reset_data();
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

    void evaluate(ZGenerationId id, uint32_t seqnum);

    void initial_compression_status(CompressionStatus status) {
        _status[0] = _status[1] = status;
    }

private:
    void ex_handle_object_common(ZGenerationId id, oop obj, InstanceKlass* ik);
    void ex_handle_seqnum(ZGenerationId id, uint32_t seqnum);
    void reset_data(ZGenerationId id);
};

class ExCompressionGains : public CHeapObj<MEMFLAGS_VALUE> {
    size_t _min_compression;
    size_t _max_compression;
    size_t _uncompressed_size;
    u2     _num_reference_fields;
public:
    void set_compression(size_t min_compression, size_t max_compression) {
        _min_compression = min_compression;
        _max_compression = max_compression;
        assert(_min_compression <= max_compression, "sanity");
    }
    void set_num_reference_fields(u2 num_reference_fields) {
        _num_reference_fields = num_reference_fields;
    }
    void set_uncompressed_size(size_t uncompressed_size) {
        _uncompressed_size = uncompressed_size;
    }
    size_t get_uncompressed_size() {
        return _uncompressed_size;
    }
    size_t get_min_comperssion() {
       return _min_compression;
    }
    size_t get_max_comperssion() {
       return _max_compression;
    }
    u2 get_num_reference_fields() {
        return _num_reference_fields;
    }
};

class ExCompressionHeuristics : StackObj {
    friend class ExCompressionGains;
private:
    static inline ExCompressedStatsData::CompressionStatus log_rejected(InstanceKlass* ik, const char* reason);
    static inline  ExCompressedStatsData::CompressionStatus log_selected(ExCompressedStatsData::CompressionStatus status, InstanceKlass* ik, const char* reason);
    static inline  ExCompressedStatsData::CompressionStatus should_consider_compression_of_loaded_instance_klass(InstanceKlass* ik, ExCompressionGains* compression_gains);
public:
    static inline void handle_loaded_instance_class(InstanceKlass* ik, ExCompressionGains*& compression_gains);
};


#endif // SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP