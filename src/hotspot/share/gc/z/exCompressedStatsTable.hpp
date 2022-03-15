/*
*/

#ifndef SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP
#define SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP

#include "memory/allocation.hpp"
#include "oops/klass.hpp"

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
};

class ExCompressedStatsData : public CHeapObj<MEMFLAGS_VALUE> {
    Klass* _klass;
    size_t _min[2], _max[2];
    size_t _num_instances[2];
    size_t _distribution[HeapWordSize][2];

public:
    ExCompressedStatsData(Klass* klass);
    ~ExCompressedStatsData();

    Klass* klass() const { return _klass; }
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
public:
    static inline bool consider_compression(ExCompressionGains* compression_gains) {
        assert(compression_gains->get_min_comperssion() <= compression_gains->get_max_comperssion(), "sanity");
        if (compression_gains->get_num_reference_fields() == 0 || compression_gains->get_max_comperssion() == 0) {
            assert(compression_gains->get_min_comperssion() == 0 &&
                    compression_gains->get_max_comperssion() == 0, "sanity");
            return false;
        }
        return true;
    }
};


#endif // SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_HPP