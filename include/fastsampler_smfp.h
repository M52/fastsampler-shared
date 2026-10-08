// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_smfp.h
// Created on: 2026-05-28
// Define spectral fingerprints as level-normalized, third-octave band
// measurements.
// =============================================================================

#pragma once
#include <stdint.h>
#include "fastsampler_format_limits.h"

// =============================================================================
// Version 4 stores dB levels at absolute frequencies so captures at different
// sample rates can be compared.
// Reject version 3 because its FFT bins have no recorded sample rate.
// =============================================================================

#define SMFP_MAGIC_V4 0x34504653
#define SMFP_VERSION  4

// =============================================================================
// Band b is centered at SMFP_REF_HZ * 2^((b - SMFP_REF_BAND) /
// SMFP_BANDS_PER_OCTAVE) Hz.
// =============================================================================
#define SMFP_BAND_COUNT 31
#define SMFP_BANDS_PER_OCTAVE 3
#define SMFP_REF_BAND 17
#define SMFP_REF_HZ 1000.0f

float smfp_band_center_hz(int band);

struct SMFPHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t zone_count;
    uint32_t band_count;
    char     collection_name[MAX_NAME_LENGTH];
};

struct SMFPZoneEntry {
    char     file_path[FS_FORMAT_PATH_LENGTH];
    uint32_t total_frames;
    uint32_t sample_rate;
    // =============================================================================
    // Zero analyzed frames means a flat fallback fingerprint.
    // =============================================================================
    uint32_t analysis_frames;
    int32_t  root_note;
    int32_t  low_velocity;
    int32_t  high_velocity;

    // =============================================================================
    // Normalize the energy-weighted mean to 0 dB. Morphing then changes timbre
    // independently of the volume crossfade.
    // =============================================================================
    float    band_db[SMFP_BAND_COUNT];
};

bool smfp_load_file(
    const char* filepath,
    SMFPHeader* out_header,
    SMFPZoneEntry* out_entries,
    uint32_t max_entries,
    uint32_t* out_entry_count
);

bool smfp_save_file(
    const char* filepath,
    const SMFPHeader* header,
    const SMFPZoneEntry* entries,
    uint32_t entry_count
);
