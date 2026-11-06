// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
#pragma once
#include <cstdint>
#include "fastsampler_format_limits.h"
#define FSI_MAGIC_PB 0x50495346
#define FSP_MAGIC_PB 0x50505346

// =============================================================================
// Each peak array contains zone_count * ZONE_PEAK_CACHE_POINTS floats in zone
// order.
// =============================================================================
bool fsp_pb_write(
    const char* filepath,
    const float* min_peaks,
    const float* max_peaks,
    uint32_t zone_count
);

// =============================================================================
// Each destination peak array needs max_zones * ZONE_PEAK_CACHE_POINTS floats.
// =============================================================================
bool fsp_pb_read(
    const char* filepath,
    float* out_min_peaks,
    float* out_max_peaks,
    uint32_t max_zones,
    uint32_t* out_zone_count
);

// =============================================================================
// Where the zones of a .fsp are, so a reader can decode one zone without the
// others. Each zone is a record of the payload; its peaks are the bytes from
// the record's offset, of the record's length. When all records have the same
// length and follow each other, stride is the distance between them, offsets
// and lengths are null, and record z starts at first + z * stride. Otherwise
// stride is 0 and offsets and lengths hold zone_count entries each.
// =============================================================================
struct FSPIndex {
    uint32_t  zone_count;
    uint32_t  first;
    uint32_t  stride;
    uint32_t  length;
    uint32_t* offsets;
    uint32_t* lengths;
};

// =============================================================================
// Index the .fsp at filepath. Only the record headers are read, so the cost
// does not grow with the peaks. Return false for a file that is not a .fsp or
// whose records do not fit in its payload. Free the index with
// fsp_pb_index_free, also after a failure.
// =============================================================================
bool fsp_pb_index(const char* filepath, FSPIndex* out_index);
void fsp_pb_index_free(FSPIndex* index);

// =============================================================================
// Decode zone `zone` of the indexed .fsp at filepath. Each destination array
// needs ZONE_PEAK_CACHE_POINTS floats. A zone with fewer points gives zeros
// for the rest, as fsp_pb_read does.
// =============================================================================
bool fsp_pb_read_zone(
    const char* filepath,
    const FSPIndex* index,
    uint32_t zone,
    float* out_min_peaks,
    float* out_max_peaks
);

// =============================================================================
// Write a .fsp of zone_count zones, asking source for the peaks of each zone
// in order. source fills two arrays of ZONE_PEAK_CACHE_POINTS floats and
// returns false to stop the write. The bytes are those fsp_pb_write writes
// for the same peaks.
// =============================================================================
typedef bool (*FSPZoneSource)(void* user, uint32_t zone, float* out_min_peaks, float* out_max_peaks);

bool fsp_pb_write_zones(
    const char* filepath,
    uint32_t zone_count,
    FSPZoneSource source,
    void* user
);
