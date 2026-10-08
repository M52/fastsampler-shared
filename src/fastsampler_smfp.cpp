// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_smfp.cpp
// Created on: 2026-05-29
// Read and write spectral fingerprints with fixed binary layouts.
// =============================================================================

#include <cstdio>
#include <cstring>
#include <cmath>
#include "../include/fastsampler_smfp.h"

// =============================================================================
// These records are written as raw bytes. A layout change requires a file
// format version change.
// =============================================================================
static_assert(sizeof(SMFPHeader) == 80, "SMFPHeader layout changed; bump SMFP_MAGIC_V4");
static_assert(sizeof(SMFPZoneEntry) == 408, "SMFPZoneEntry layout changed; bump SMFP_MAGIC_V4");

float smfp_band_center_hz(int band) {
    if (band < 0) band = 0;
    if (band >= SMFP_BAND_COUNT) band = SMFP_BAND_COUNT - 1;
    return SMFP_REF_HZ * exp2f((float)(band - SMFP_REF_BAND) / (float)SMFP_BANDS_PER_OCTAVE);
}

bool smfp_load_file(
    const char* filepath,
    SMFPHeader* out_header,
    SMFPZoneEntry* out_entries,
    uint32_t max_entries,
    uint32_t* out_entry_count
) {
    if (!filepath || !out_header || !out_entries || !out_entry_count) return false;

    FILE* f = fopen(filepath, "rb");
    if (!f) return false;

    SMFPHeader header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return false;
    }

    // =============================================================================
    // Validate band count as well as magic because the band index determines
    // its frequency.
    // =============================================================================
    if (header.magic != SMFP_MAGIC_V4 ||
        header.version != SMFP_VERSION ||
        header.band_count != SMFP_BAND_COUNT) {
        fclose(f);
        return false;
    }

    *out_header = header;
    uint32_t count = header.zone_count;
    if (count > max_entries) {
        count = max_entries;
    }

    if (count > 0 && fread(out_entries, sizeof(SMFPZoneEntry), count, f) != count) {
        fclose(f);
        return false;
    }

    *out_entry_count = count;
    fclose(f);
    return true;
}

bool smfp_save_file(
    const char* filepath,
    const SMFPHeader* header,
    const SMFPZoneEntry* entries,
    uint32_t entry_count
) {
    if (!filepath || !header || (!entries && entry_count > 0)) return false;

    FILE* f = fopen(filepath, "wb");
    if (!f) return false;

    if (fwrite(header, sizeof(SMFPHeader), 1, f) != 1) {
        fclose(f);
        return false;
    }

    if (entry_count > 0 && fwrite(entries, sizeof(SMFPZoneEntry), entry_count, f) != entry_count) {
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}
