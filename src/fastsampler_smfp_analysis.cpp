// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_smfp_analysis.cpp
// Created on: 2026-09-14
// Measure the spectral envelope of a zone into spectral fingerprint bands.
// =============================================================================

#include <cmath>
#include <cstddef>
#include <cstring>
#include "../include/fastsampler_smfp_analysis.h"

// =============================================================================
// Stored fingerprints were measured with these constants. Changing one makes
// new measurements disagree with existing files.
// =============================================================================
#define SMFP_TWO_PI                 (2.0f * 3.14159265358979323846f)
#define SMFP_A4_HZ                  440.0f
#define SMFP_A4_NOTE                69
#define SMFP_SEMITONES_PER_OCTAVE   12

// =============================================================================
// Convert natural-log magnitude to dB with 20 / ln(10).
// =============================================================================
#define SMFP_NEPER_TO_DB            8.685889638f

#define SMFP_LOG_FLOOR_NEPERS       9.2103404f

#define SMFP_ENVELOPE_MIN_HALF_BINS 2
#define SMFP_ENVELOPE_MAX_HALF_BINS 256

#define SMFP_BAND_DB_MIN            (-120.0f)
#define SMFP_BAND_DB_MAX            60.0f

static unsigned int smfp_bit_reverse(unsigned int x, int bits) {
    unsigned int y = 0;
    for (int i = 0; i < bits; ++i) {
        y = (y << 1) | (x & 1);
        x >>= 1;
    }
    return y;
}

void smfp_fft(float* real, float* imag, int bits) {
    int N = 1 << bits;

    for (int i = 0; i < N; ++i) {
        int rev = (int)smfp_bit_reverse((unsigned int)i, bits);
        if (i < rev) {
            float tr = real[i];
            float ti = imag[i];
            real[i] = real[rev];
            imag[i] = imag[rev];
            real[rev] = tr;
            imag[rev] = ti;
        }
    }

    for (int k = 1; k <= bits; ++k) {
        int m = 1 << k;
        int half_m = m >> 1;
        float w_r = cosf(-SMFP_TWO_PI / m);
        float w_i = sinf(-SMFP_TWO_PI / m);

        for (int i = 0; i < N; i += m) {
            float w_curr_r = 1.0f;
            float w_curr_i = 0.0f;

            for (int j = 0; j < half_m; ++j) {
                int even = i + j;
                int odd = i + j + half_m;

                float t_r = w_curr_r * real[odd] - w_curr_i * imag[odd];
                float t_i = w_curr_r * imag[odd] + w_curr_i * real[odd];

                real[odd] = real[even] - t_r;
                imag[odd] = imag[even] - t_i;
                real[even] += t_r;
                imag[even] += t_i;

                float next_w_r = w_curr_r * w_r - w_curr_i * w_i;
                float next_w_i = w_curr_r * w_i + w_curr_i * w_r;
                w_curr_r = next_w_r;
                w_curr_i = next_w_i;
            }
        }
    }
}

void smfp_analysis_scratch_init(SMFPAnalysisScratch* scratch) {
    if (!scratch) return;
    for (int i = 0; i < SMFP_ANALYSIS_FFT_SIZE; ++i) {
        scratch->window[i] = 0.5f * (1.0f - cosf(SMFP_TWO_PI * (float)i /
                                                 (float)(SMFP_ANALYSIS_FFT_SIZE - 1)));
    }
}

void smfp_resolve_zone_range(
    uint32_t total_frames,
    uint32_t play_start,
    uint32_t play_end,
    bool loop_enabled,
    uint32_t loop_start,
    uint32_t loop_end,
    const SMFPAnalysisRange* range,
    uint32_t* out_start,
    uint32_t* out_end)
{
    const SMFPAnalysisRange automatic = { false, SMFP_DEFAULT_RANGE_START, SMFP_DEFAULT_RANGE_END };
    if (!range) range = &automatic;

    uint32_t p_start = play_start;
    uint32_t p_end = (play_end == 0 || play_end > total_frames) ? total_frames : play_end;
    if (p_start >= p_end) {
        p_start = 0;
        p_end = total_frames;
    }

    uint32_t s, e;
    bool use_loop = !range->manual && loop_enabled &&
                    loop_end > loop_start && loop_end <= total_frames;
    if (use_loop) {
        s = loop_start;
        e = loop_end;
    } else {
        float sf = range->start_frac;
        float ef = range->end_frac;
        if (sf < 0.0f) sf = 0.0f;
        if (sf > 1.0f) sf = 1.0f;
        if (ef < 0.0f) ef = 0.0f;
        if (ef > 1.0f) ef = 1.0f;
        if (ef <= sf) {
            sf = SMFP_DEFAULT_RANGE_START;
            ef = SMFP_DEFAULT_RANGE_END;
        }
        uint32_t span = p_end - p_start;
        s = p_start + (uint32_t)(sf * (float)span);
        e = p_start + (uint32_t)(ef * (float)span);
    }

    if (e > total_frames) e = total_frames;
    if (s >= e) {
        s = p_start;
        e = p_end;
    }

    if (e - s < (uint32_t)SMFP_ANALYSIS_FFT_SIZE) {
        s = p_start;
        e = p_end;
    }

    *out_start = s;
    *out_end = e;
}

float smfp_root_note_hz(int root_note) {
    return SMFP_A4_HZ * exp2f(((float)root_note - (float)SMFP_A4_NOTE) / (float)SMFP_SEMITONES_PER_OCTAVE);
}

uint32_t smfp_analyze_zone_bands(
    SMFPAnalysisScratch* scratch,
    const float* pcm,
    uint32_t total_frames,
    uint32_t channels,
    uint32_t sample_rate,
    uint32_t range_start,
    uint32_t range_end,
    float f0_hz,
    float* out_band_db)
{
    if (!out_band_db) return 0;
    for (int b = 0; b < SMFP_BAND_COUNT; ++b) out_band_db[b] = 0.0f;

    if (!scratch || !pcm || channels == 0 || sample_rate == 0) return 0;
    if (range_end > total_frames) range_end = total_frames;
    if (range_start >= range_end) return 0;
    if (range_end - range_start < (uint32_t)SMFP_ANALYSIS_FFT_SIZE) return 0;

    const int N = SMFP_ANALYSIS_FFT_SIZE;

    // =============================================================================
    // Average channel power independently. Summing stereo samples first can
    // cancel high-frequency content.
    // =============================================================================
    memset(scratch->power, 0, sizeof(scratch->power));
    uint32_t window_count = 0;

    for (uint32_t pos = range_start; pos + (uint32_t)N <= range_end; pos += SMFP_ANALYSIS_HOP) {
        for (uint32_t c = 0; c < channels; ++c) {
            const float* src = pcm + (size_t)pos * channels + c;
            for (int i = 0; i < N; ++i) {
                scratch->real[i] = src[(size_t)i * channels] * scratch->window[i];
            }
            memset(scratch->imag, 0, sizeof(scratch->imag));

            smfp_fft(scratch->real, scratch->imag, SMFP_ANALYSIS_FFT_BITS);

            for (int k = 0; k < SMFP_ANALYSIS_BINS; ++k) {
                scratch->power[k] += scratch->real[k] * scratch->real[k] +
                                     scratch->imag[k] * scratch->imag[k];
            }
        }
        window_count++;
    }

    if (window_count == 0) return 0;

    float inv = 1.0f / (float)(window_count * channels);
    float peak_log = -1e30f;
    for (int k = 0; k < SMFP_ANALYSIS_BINS; ++k) {
        float v = 0.5f * logf(scratch->power[k] * inv + 1e-20f);
        scratch->log_mag[k] = v;
        if (v > peak_log) peak_log = v;
    }

    // =============================================================================
    // Limit log magnitude to 80 dB below the peak so deep harmonic gaps do not
    // dominate envelope extraction.
    // =============================================================================
    float floor_log = peak_log - SMFP_LOG_FLOOR_NEPERS;
    for (int k = 0; k < SMFP_ANALYSIS_BINS; ++k) {
        if (scratch->log_mag[k] < floor_log) scratch->log_mag[k] = floor_log;
    }

    // =============================================================================
    // Use a running maximum approximately one harmonic spacing wide to extract
    // the envelope above the harmonic peaks.
    // Derive width from root pitch so the window follows changing harmonic
    // spacing.
    // =============================================================================
    int half = SMFP_ENVELOPE_MIN_HALF_BINS;
    if (f0_hz > 1.0f) half = (int)(0.5f * f0_hz * (float)N / (float)sample_rate);
    if (half < SMFP_ENVELOPE_MIN_HALF_BINS) half = SMFP_ENVELOPE_MIN_HALF_BINS;
    if (half > SMFP_ENVELOPE_MAX_HALF_BINS) half = SMFP_ENVELOPE_MAX_HALF_BINS;

    for (int k = 0; k < SMFP_ANALYSIS_BINS; ++k) {
        int lo = k - half; if (lo < 0) lo = 0;
        int hi = k + half; if (hi > SMFP_ANALYSIS_BINS - 1) hi = SMFP_ANALYSIS_BINS - 1;

        float peak = scratch->log_mag[lo];
        for (int j = lo + 1; j <= hi; ++j) {
            if (scratch->log_mag[j] > peak) peak = scratch->log_mag[j];
        }
        scratch->real[k] = peak;
    }

    // =============================================================================
    // Smooth the maximum envelope with a box average of the same width to
    // reduce steps.
    // =============================================================================
    float run = 0.0f;
    for (int k = 0; k <= half && k < SMFP_ANALYSIS_BINS; ++k) run += scratch->real[k];
    int count = (half + 1 < SMFP_ANALYSIS_BINS) ? half + 1 : SMFP_ANALYSIS_BINS;

    for (int k = 0; k < SMFP_ANALYSIS_BINS; ++k) {
        scratch->log_mag[k] = run / (float)count;

        int add = k + half + 1;
        int drop = k - half;
        if (add < SMFP_ANALYSIS_BINS) { run += scratch->real[add]; count++; }
        if (drop >= 0) { run -= scratch->real[drop]; count--; }
    }

    // =============================================================================
    // Interpolate the smoothed envelope at band centers. Some low-frequency
    // bands are narrower than one FFT bin.
    // =============================================================================
    float bins_per_hz = (float)N / (float)sample_rate;
    float max_bin = (float)(SMFP_ANALYSIS_BINS - 1);

    for (int b = 0; b < SMFP_BAND_COUNT; ++b) {
        float bin = smfp_band_center_hz(b) * bins_per_hz;
        if (bin < 0.0f) bin = 0.0f;
        if (bin > max_bin) bin = max_bin;

        int i0 = (int)bin;
        int i1 = i0 + 1;
        if (i1 > SMFP_ANALYSIS_BINS - 1) i1 = SMFP_ANALYSIS_BINS - 1;
        float t = bin - (float)i0;

        float v = scratch->log_mag[i0] * (1.0f - t) + scratch->log_mag[i1] * t;
        out_band_db[b] = v * SMFP_NEPER_TO_DB;
    }

    // =============================================================================
    // Normalize mean band power so fingerprint ratios describe timbre
    // independently of recording level.
    // =============================================================================
    double total_power = 0.0;
    for (int b = 0; b < SMFP_BAND_COUNT; ++b) {
        total_power += pow(10.0, (double)out_band_db[b] * 0.1);
    }
    if (total_power > 0.0) {
        float mean_db = (float)(10.0 * log10(total_power / (double)SMFP_BAND_COUNT));
        for (int b = 0; b < SMFP_BAND_COUNT; ++b) out_band_db[b] -= mean_db;
    }

    for (int b = 0; b < SMFP_BAND_COUNT; ++b) {
        if (out_band_db[b] < SMFP_BAND_DB_MIN) out_band_db[b] = SMFP_BAND_DB_MIN;
        if (out_band_db[b] > SMFP_BAND_DB_MAX) out_band_db[b] = SMFP_BAND_DB_MAX;
    }

    return window_count * SMFP_ANALYSIS_HOP;
}
