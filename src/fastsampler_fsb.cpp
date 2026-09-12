// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_fsb.cpp
// Created on: 2026-05-28
// Validate soundbank headers and resolve shared sample and stream locations.
// =============================================================================

#include <cstring>
#include "../include/fastsampler_fsb.h"

// =============================================================================
// Map each version 1 zone to an independent version 2 sample.
// =============================================================================
static void fsb_zone_entry_from_v1(const FSBZoneEntryV1& v1, uint32_t index, FSBZoneEntry* out) {
    memcpy(out->name, v1.name, sizeof(out->name));
    out->preload_frame_offset = v1.preload_frame_offset;
    out->preload_frame_count  = v1.preload_frame_count;
    out->total_frames         = v1.total_frames;
    out->sample_frame_offset  = 0;
    out->sample_index         = index;
    out->sample_frames        = (uint32_t)v1.total_frames;
    out->stream_offset        = v1.stream_offset;
    out->stream_size          = v1.stream_size;
    out->stream_first_frame   = v1.preload_frame_count;
}

bool fsb_load_headers(const uint8_t* fsb_map_view, uint64_t fsb_map_size, FSBHeader* out_header, FSBZoneEntry* out_zones, uint32_t max_zones, uint32_t expected_zone_count) {
    if (!fsb_map_view || !out_header || !out_zones) return false;
    if (fsb_map_size < sizeof(FSBHeader)) return false;

    memcpy(out_header, fsb_map_view, sizeof(FSBHeader));
    if (out_header->magic != FSB_MAGIC) return false;
    if (out_header->version != FSB_VERSION && out_header->version != FSB_VERSION_PER_ZONE) return false;
    bool is_v1 = (out_header->version == FSB_VERSION_PER_ZONE);

    if (out_header->channels != 1 && out_header->channels != 2) return false;
    if (out_header->bps != 16 && out_header->bps != 24) return false;
    if (out_header->sample_rate < FSB_MIN_SAMPLE_RATE || out_header->sample_rate > FSB_MAX_SAMPLE_RATE) return false;

    if (out_header->zone_count > max_zones || out_header->zone_count < expected_zone_count) {
        return false;
    }

    if (out_header->preload_offset > fsb_map_size ||
        out_header->preload_size > fsb_map_size ||
        (out_header->preload_offset + out_header->preload_size) > fsb_map_size ||
        (out_header->preload_offset + out_header->preload_size) < out_header->preload_offset) {
        return false;
    }

    size_t entry_size = is_v1 ? sizeof(FSBZoneEntryV1) : sizeof(FSBZoneEntry);
    size_t required_size = sizeof(FSBHeader) + entry_size * out_header->zone_count;
    if (fsb_map_size < required_size) return false;

    const uint8_t* rows = fsb_map_view + sizeof(FSBHeader);
    for (uint32_t i = 0; i < out_header->zone_count; ++i) {
        if (is_v1) {
            FSBZoneEntryV1 v1;
            memcpy(&v1, rows + (size_t)i * sizeof(FSBZoneEntryV1), sizeof(FSBZoneEntryV1));
            fsb_zone_entry_from_v1(v1, i, &out_zones[i]);
        } else {
            memcpy(&out_zones[i], rows + (size_t)i * sizeof(FSBZoneEntry), sizeof(FSBZoneEntry));
        }
    }

    // =============================================================================
    // Use the preload block size. Summed row lengths would count shared frames
    // more than once.
    // =============================================================================
    uint64_t preload_total_frames = fsb_preload_total_frames(*out_header);

    uint64_t expected_offset = 0;
    for (uint32_t i = 0; i < out_header->zone_count; ++i) {
        const FSBZoneEntry& z = out_zones[i];

        // =============================================================================
        // Version 1 requires sequential, non-overlapping preload regions.
        // =============================================================================
        if (is_v1) {
            if (z.preload_frame_offset != expected_offset) return false;
            expected_offset += z.preload_frame_count;
        }

        if (z.preload_frame_offset + z.preload_frame_count < z.preload_frame_offset) return false;
        if (z.preload_frame_offset + z.preload_frame_count > preload_total_frames) return false;

        if (z.preload_frame_count > z.total_frames) return false;

        if (z.sample_frame_offset + z.total_frames < z.sample_frame_offset) return false;
        if (z.sample_frame_offset + z.total_frames > z.sample_frames) return false;
        if (z.sample_index >= out_header->zone_count) return false;
        if (z.stream_first_frame > z.sample_frames) return false;

        if (z.stream_offset > fsb_map_size ||
            z.stream_size > fsb_map_size ||
            (z.stream_offset + z.stream_size) > fsb_map_size ||
            (z.stream_offset + z.stream_size) < z.stream_offset) {
            return false;
        }

        // =============================================================================
        // Convert the zone's stream start to the shared stream's frame
        // coordinates.
        // =============================================================================
        uint64_t zone_stream_first = z.sample_frame_offset + z.preload_frame_count;
        if (z.preload_frame_count < z.total_frames && zone_stream_first < z.stream_first_frame) {
            return false;
        }
    }

    return true;
}

bool fsb_ram_only_sample_bases(const FSBZoneEntry* zones, uint32_t zone_count,
                               uint64_t preload_total_frames, uint64_t* out_bases) {
    if (!zones || !out_bases || zone_count == 0) return false;

    for (uint32_t i = 0; i < zone_count; ++i) {
        out_bases[i] = FSB_SAMPLE_BASE_UNSET;
    }

    for (uint32_t i = 0; i < zone_count; ++i) {
        const FSBZoneEntry& z = zones[i];

        if (z.preload_frame_count != z.total_frames) return false;
        if (z.sample_index >= zone_count) return false;

        // =============================================================================
        // Subtract the zone offset to recover the shared sample base.
        // =============================================================================
        if (z.preload_frame_offset < z.sample_frame_offset) return false;
        uint64_t base = z.preload_frame_offset - z.sample_frame_offset;
        if (base + z.sample_frames > preload_total_frames) return false;

        // =============================================================================
        // All rows for one sample must resolve to the same base.
        // =============================================================================
        if (out_bases[z.sample_index] != FSB_SAMPLE_BASE_UNSET &&
            out_bases[z.sample_index] != base) {
            return false;
        }
        out_bases[z.sample_index] = base;
    }

    return true;
}
