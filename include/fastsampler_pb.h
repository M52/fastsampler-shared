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
