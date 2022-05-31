/*
*/

#ifndef SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_INLINE_HPP
#define SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_INLINE_HPP

#include "gc/z/exCompressedStatsTable.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "gc/shared/gc_globals.hpp"


inline ExCompressedStatsData::CompressionStatus ExCompressionHeuristics::log_rejected(InstanceKlass* ik, const char* reason) {
    LogTarget(Info, gc, coops) lt;
    if (lt.is_enabled()) {
        ResourceMark rm;
        lt.print("[Rejecting compression of %s]: %s", ik->external_name(), reason);
    }
    return ExCompressedStatsData::CompressNever;
}
inline  ExCompressedStatsData::CompressionStatus ExCompressionHeuristics::log_selected(ExCompressedStatsData::CompressionStatus status, InstanceKlass* ik, const char* reason) {
    assert(status != ExCompressedStatsData::CompressAbort, "sanity");
    assert(status != ExCompressedStatsData::CompressPending, "sanity");
    assert(status != ExCompressedStatsData::CompressComplete, "sanity");
    LogTarget(Info, gc, coops) lt;
    if (lt.is_enabled()) {
        ResourceMark rm;
        lt.print("[Consider compression of %s]: %s", ik->external_name(), reason);
    }
    return status;
}
inline ExCompressedStatsData::CompressionStatus ExCompressionHeuristics::should_consider_compression_of_loaded_instance_klass(InstanceKlass* ik, ExCompressionGains* compression_gains) {
    if (!ExCompressInternals) {
        Symbol* name = ik->name();
        if (name->starts_with("java/") || name->starts_with("jdk/") ||name->starts_with("sun/")) {
            return log_rejected(ik, "Internal Klass");
        }
    }
    if (compression_gains->get_num_reference_fields() == 0) {
        return log_rejected(ik, "No reference fields");
    }
    if (compression_gains->get_max_comperssion() == 0) {
        return log_rejected(ik, "Incompressible");
    }
    return log_selected(ExCompressedStatsData::CompressLikely, ik, "UNKOWNREASON");
}
inline void ExCompressionHeuristics::handle_loaded_instance_class(InstanceKlass* ik, ExCompressionGains*& compression_gains) {
    LogTarget(Info, gc, coops) lt;
    if (lt.is_enabled()) {
        ResourceMark rm;
        lt.print("[Compression Gains for %s]: %d-(%d,%d) over %hd fields", ik->external_name(),
            compression_gains->get_uncompressed_size(),
            compression_gains->get_min_comperssion(), compression_gains->get_max_comperssion(),
            compression_gains->get_num_reference_fields());
    }
    auto status = ExCompressionHeuristics::should_consider_compression_of_loaded_instance_klass(ik, compression_gains);
    if (status != ExCompressedStatsData::CompressNever) {
        ik->set_compressed_stats_data_entry(ExCompressedStatsTable::new_entry(ik));
        ik->compressed_stats_data_entry()->initial_compression_status(status);
    } else {
        delete compression_gains;
        compression_gains = NULL;
    }
    ik->set_compression_gains(compression_gains);
    compression_gains = NULL;
}

inline void ExCompressionHeuristics::handle_mark_object(oop obj, ZGenerationId id, uint32_t seqnum) {
  if (ExUseDynamicCompressedOops)  {
    if (obj == NULL) {
        return;
    }
    ExCompressedStatsTable::register_mark_object(obj, id);
    if (obj->is_instance()) {
        InstanceKlass::cast(obj->klass())->ex_handle_object(id, seqnum, obj);
    }
    if (obj->is_objArray()) {
        const ObjArrayKlass* oak = ObjArrayKlass::cast(obj->klass());
        const Klass* bk = oak->bottom_klass();
        if (bk->is_instance_klass()) {
            oak->ex_handle_object(id, seqnum, obj);
        } else if (ExCompressObjArrayOfTypeArrays) {
            assert(bk->is_typeArray_klass(), "invariant");
            oak->ex_handle_object(id, seqnum, obj);
        }
    }
  }
}

inline void ExCompressionHeuristics::initialize() {
    if (UseZGC && ExUseDynamicCompressedOops) {
        init_max_address_size();
        log_info(gc,coops)("Virtual Heap Size: %ld MB",  get_max_address_size()/ M);
        log_info(gc,coops)("Max Delta in Heap: %ld M", get_max_address_size_delta() / M);
        log_info(gc,coops)("Assumed Max Bytes: %ld B / Reference", get_max_bytes_per_reference());
        ExCompressedStatsTable::create_table();
        if (ExVerifyAllStores) {
            ExCompressedStatsTable::verify_init();
        }
    }
}

inline void ExCompressionHeuristics::handle_create_objarray_class(ObjArrayKlass* oak) {
    LogTarget(Info, gc, coops) lt;
    if (lt.is_enabled()) {
        ResourceMark rm;
        lt.print("[Create ObjArray %s]", oak->external_name());
    }
    if (!ExCompressObjArrayOfInternals) {
        Symbol* name = oak->bottom_klass()->name();
        if (name->starts_with("java/") || name->starts_with("jdk/") ||name->starts_with("sun/")) {
            if (lt.is_enabled()) {
                ResourceMark rm;
                lt.print("[Not compressiong ObjArray %s]: Internal Klass", oak->external_name());
            }
            return;
        }
    }
    if (!ExCompressObjArrayOfTypeArrays && oak->bottom_klass()->is_typeArray_klass()) {
        return;
    }
    oak->set_compressed_stats_data_entry(ExCompressedStatsTable::new_entry(oak));
    oak->compressed_stats_data_entry()->initial_compression_status(ExCompressedStatsData::CompressEvaluate);
}

inline void ExCompressionHeuristics::ex_verify_store(zaddress src, zpointer* dst) {
    if (src != zaddress::null) {
        // zaddress dst_addr = ZBarrier::load_barrier_on_oop_field(dst);
        ZPage* page = ZHeap::heap()->page(dst);
        if (page == NULL) {
            return;
        }
        ZGeneration* generation = ZGeneration::generation(page->generation_id());
        if (generation->is_phase_mark()) {
            return;
        }
        ExCompressedStatsTable::verify_register_store(generation->id(), (size_t)dst, (size_t)src);
    }
}

#endif // SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_INLINE_HPP