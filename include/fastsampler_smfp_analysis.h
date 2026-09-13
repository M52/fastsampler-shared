// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_smfp_analysis.h
// Created on: 2026-09-14
// Measure the spectral envelope of a zone into spectral fingerprint bands.
// FastSampler measures the collection that carries a spectral morph filter
// when it loads it, and compares that with fingerprint files other programs
// wrote. Every program that writes or compares fingerprints measures with
// this code.
// =============================================================================

#pragma once
#include <stdint.h>
#include "fastsampler_smfp.h"

// =============================================================================
// Hann windows of 4096 frames, a quarter of a window apart.
// =============================================================================
#define SMFP_ANALYSIS_FFT_BITS 12
#define SMFP_ANALYSIS_FFT_SIZE (1 << SMFP_ANALYSIS_FFT_BITS)
#define SMFP_ANALYSIS_BINS     (SMFP_ANALYSIS_FFT_SIZE / 2 + 1)
#define SMFP_ANALYSIS_HOP      (SMFP_ANALYSIS_FFT_SIZE / 4)

// =============================================================================
// The part of the playing region that a zone without a usable loop is
// measured over.
// =============================================================================
#define SMFP_DEFAULT_RANGE_START 0.15f
#define SMFP_DEFAULT_RANGE_END   0.85f

// =============================================================================
// An automatic range uses the zone's loop when it has a valid one. A manual
// range, and an automatic range without a loop, use the fractions of the
// playing region.
// =============================================================================
struct SMFPAnalysisRange {
    bool  manual;
    float start_frac;
    float end_frac;
};

// =============================================================================
// Work buffers for one analysis at a time, about 64 KB. Fill the window with
// smfp_analysis_scratch_init() before the first analysis.
// =============================================================================
struct SMFPAnalysisScratch {
    float real[SMFP_ANALYSIS_FFT_SIZE];
    float imag[SMFP_ANALYSIS_FFT_SIZE];
    float window[SMFP_ANALYSIS_FFT_SIZE];
    float power[SMFP_ANALYSIS_BINS];
    float log_mag[SMFP_ANALYSIS_BINS];
};

void smfp_analysis_scratch_init(SMFPAnalysisScratch* scratch);

// =============================================================================
// The frames [out_start, out_end) of a zone to measure. A play_end of zero
// means the end of the zone. A null range is the automatic range with the
// default fractions. A range shorter than one window widens to the whole
// playing region.
// =============================================================================
void smfp_resolve_zone_range(
    uint32_t total_frames,
    uint32_t play_start,
    uint32_t play_end,
    bool loop_enabled,
    uint32_t loop_start,
    uint32_t loop_end,
    const SMFPAnalysisRange* range,
    uint32_t* out_start,
    uint32_t* out_end);

// =============================================================================
// The fundamental of a root note, with A4 at 440 Hz. The envelope follows the
// harmonic spacing it gives.
// =============================================================================
float smfp_root_note_hz(int root_note);

// =============================================================================
// Measure frames [range_start, range_end) of interleaved pcm into
// SMFP_BAND_COUNT levels with a mean band power of 0 dB. Return the analyzed
// frame count. A range shorter than one window gives flat bands and zero.
// An f0_hz of zero or less uses the narrowest envelope.
// =============================================================================
uint32_t smfp_analyze_zone_bands(
    SMFPAnalysisScratch* scratch,
    const float* pcm,
    uint32_t total_frames,
    uint32_t channels,
    uint32_t sample_rate,
    uint32_t range_start,
    uint32_t range_end,
    float f0_hz,
    float* out_band_db);

// =============================================================================
// Transform 2^bits points in place with an unnormalized forward FFT. Bin k is
// the sum of x[n] * e^(-2 pi i k n / 2^bits).
// =============================================================================
void smfp_fft(float* real, float* imag, int bits);
