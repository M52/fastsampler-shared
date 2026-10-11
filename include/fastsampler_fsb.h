// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_fsb.h
// Created on: 2026-05-28
// Define soundbank layouts and validation interfaces for shared sample
// storage.
// =============================================================================

#pragma once
#include <cstdint>
#include "fastsampler_format_limits.h"
#include "fastsampler_provenance.h"

#define FSB_MAGIC 0x31425346

// =============================================================================
// Writers write version 3: encrypted provenance records immediately after
// the header, then the version 2 rows and the preload block. Provenance is
// mandatory in v3. The earlier, unused opaque-span v3 design is superseded.
// =============================================================================
#define FSB_VERSION 3

// =============================================================================
// Older versions that readers still read. Version 2 has no provenance, and
// version 1 has the older row layout.
// =============================================================================
#define FSB_VERSION_SHARED_SAMPLES 2
#define FSB_VERSION_PER_ZONE 1

#define FSB_MIN_SAMPLE_RATE 8000
#define FSB_MAX_SAMPLE_RATE 384000

// =============================================================================
// This preload value means that the complete sample is resident and needs no
// streaming.
// =============================================================================
#define FSB_PRELOAD_ALL_FRAMES 0xFFFFFFFF

#define FSB_SAMPLE_BASE_UNSET UINT64_MAX

#pragma pack(push, 1)
struct FSBHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t preload_frames;
    uint32_t zone_count;
    uint64_t preload_offset;
    uint64_t preload_size;
    uint32_t bps;
};

// =============================================================================
// Keep this layout unchanged to read version 1 banks.
// =============================================================================
struct FSBZoneEntryV1 {
    char name[MAX_NAME_LENGTH];
    uint64_t preload_frame_offset;
    uint64_t preload_frame_count;
    uint64_t stream_offset;
    uint64_t stream_size;
    uint64_t total_frames;
};

// =============================================================================
// A version 2 sample can serve several zones, each with its own playback
// window.
// =============================================================================
struct FSBZoneEntry {
    char name[MAX_NAME_LENGTH];

    uint64_t preload_frame_offset;
    uint64_t preload_frame_count;
    uint64_t total_frames;
    uint64_t sample_frame_offset;

    // =============================================================================
    // Rows with the same sample_index must agree on all stored-sample fields
    // below.
    // =============================================================================
    uint32_t sample_index;
    uint32_t sample_frames;
    uint64_t stream_offset;
    uint64_t stream_size;
    // =============================================================================
    // A single-zone stream excludes its preload. A shared stream contains the
    // complete sample because zones can start at different frames.
    // =============================================================================
    uint64_t stream_first_frame;
};
#pragma pack(pop)

// =============================================================================
// Validates public provenance framing, without decrypting it. Playback does
// not need the private key. out_provenance points into the mapped file and
// is empty for v1/v2; only use it while that mapping stays alive.
// =============================================================================
bool fsb_load_headers(const uint8_t* fsb_map_view, uint64_t fsb_map_size, FSBHeader* out_header, FSBZoneEntry* out_zones, uint32_t max_zones, uint32_t expected_zone_count);

// =============================================================================
// Reads only the provenance framing. A full bank consumer also calls
// fsb_load_headers to validate the rows and their locations.
// =============================================================================
bool fsb_read_provenance(const uint8_t* fsb_map_view, uint64_t fsb_map_size, FSBProvenanceView* out);

// =============================================================================
// out_bases needs one entry per possible sample index.
// If rows disagree on a sample base, return false; the caller must then treat
// zones as separate samples.
// =============================================================================
bool fsb_ram_only_sample_bases(const FSBZoneEntry* zones, uint32_t zone_count,
                               uint64_t preload_total_frames, uint64_t* out_bases);

inline uint32_t fsb_bytes_per_sample(uint32_t bps) {
    return (bps == 16) ? 2u : 3u;
}

// =============================================================================
// The rows follow the header and, in v3, the typed provenance records.
// A new bank normally has one record. Use the actual count for a combined
// bank, as returned by fsb_read_provenance.
// =============================================================================
inline uint64_t fsb_rows_offset(uint32_t version, uint32_t provenance_count = 1) {
    return sizeof(FSBHeader) + (version == FSB_VERSION ? fsb_provenance_bytes(provenance_count) : 0);
}

// =============================================================================
// Derive frame count from the preload block size because zone rows can
// overlap.
// =============================================================================
inline uint64_t fsb_preload_total_frames(const FSBHeader& header) {
    uint64_t frame_bytes = (uint64_t)header.channels * fsb_bytes_per_sample(header.bps);
    if (frame_bytes == 0) return 0;
    return header.preload_size / frame_bytes;
}

static_assert(sizeof(FSBHeader) == 44, "FSB header layout changed");
static_assert(sizeof(FSBZoneEntryV1) == 104, "FSB v1 row layout changed");
static_assert(sizeof(FSBZoneEntry) == 128, "FSB v2 row layout changed");
