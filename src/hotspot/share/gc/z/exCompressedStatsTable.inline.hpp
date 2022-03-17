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
        lt.print("[Compression Gains for %s]: %ld-(%ld,%ld) over %hd fields", ik->external_name(),
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

#endif // SHARE_GC_Z_EXCOMPRESSEDSTATSTABLE_INLINE_HPP