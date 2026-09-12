// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_source_format.h
// Created on: 2026-09-13
// Define persisted source record capacities and processing step IDs.
// =============================================================================

#pragma once

// =============================================================================
// Capacities of the source records in an FSI file. Keep them consistent with
// fastsampler.options.
// =============================================================================
#define FS_SOURCE_MAX_FILES         256
#define FS_SOURCE_MAX_SAMPLES       4096
#define FS_SOURCE_MAX_STEPS         8
#define FS_SOURCE_NAME_LENGTH       128

// =============================================================================
// Persisted step IDs. An ID names one exact behaviour, including how the step
// counts frames and rounds values. Never change what an ID does. Add a new ID
// for a new behaviour and keep the old one.
// =============================================================================

// channels: copy mono to both channels, or keep the first two of more.
#define FS_SOURCE_STEP_CHANNELS         1
// bits: fsbanktool's depth conversion. 24 to 16 rounds half up and holds the
// rails; 16 to 24 multiplies by 256.
#define FS_SOURCE_STEP_DEPTH_ROUND      2
// rate, bits: fsbanktool's windowed sinc resampler, rounded to bits.
#define FS_SOURCE_STEP_RESAMPLE_SINC    3
// rate, bits: FastSampler's export resampler, quantized to bits.
#define FS_SOURCE_STEP_RESAMPLE_CUBIC   4
// bits: FastSampler's export quantization of float audio to bits.
#define FS_SOURCE_STEP_DEPTH_FLOAT      5
// first, frames: keep frames starting at first.
#define FS_SOURCE_STEP_CUT              6
