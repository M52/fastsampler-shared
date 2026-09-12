// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_source.h
// Created on: 2026-09-13
// Declare the processing steps that make stored samples from source audio.
// =============================================================================

#pragma once
#include <cstddef>
#include <cstdint>
#include "fastsampler_hash.h"
#include "fastsampler_source_format.h"

// =============================================================================
// One step of a source record. Each step type uses only some fields; see
// fastsampler_source_format.h.
// =============================================================================
struct FsSourceStep {
    uint32_t type;
    uint32_t rate;
    uint32_t channels;
    uint32_t bits;
    uint32_t first;
    uint32_t frames;
};

// =============================================================================
// A whole sample: interleaved int32 values at a depth of 16 or 24 bits. The
// functions that fill one allocate data with malloc. Release it with
// fs_source_audio_free().
// =============================================================================
struct FsSourceAudio {
    int32_t* data;
    uint32_t frames;
    uint32_t channels;
    uint32_t rate;
    uint32_t bits;
};

void fs_source_audio_free(FsSourceAudio* audio);

// =============================================================================
// The format that a step makes from in. The data of in is not used. Returns
// false, with err set, when the step is not valid for in.
// =============================================================================
bool fs_source_step_shape(const FsSourceAudio* in, const FsSourceStep* step, FsSourceAudio* out,
                          char* err, size_t err_len);

// =============================================================================
// Applies the steps in order to the format of shape only, without audio.
// =============================================================================
bool fs_source_replay_shape(FsSourceAudio* shape, const FsSourceStep* steps, uint32_t step_count,
                            char* err, size_t err_len);

// =============================================================================
// Applies one step. out receives new data, and in does not change.
// =============================================================================
bool fs_source_step_apply(const FsSourceAudio* in, const FsSourceStep* step, FsSourceAudio* out,
                          char* err, size_t err_len);

// =============================================================================
// Applies the steps in order and replaces audio with the result. On failure,
// audio keeps the result of the last step that succeeded.
// =============================================================================
bool fs_source_replay(FsSourceAudio* audio, const FsSourceStep* steps, uint32_t step_count,
                      char* err, size_t err_len);

// =============================================================================
// A fingerprint covers interleaved values at their own depth, each value as four
// little-endian bytes, hashed with fs_hash64.
// =============================================================================
uint64_t fs_source_fingerprint(const int32_t* values, uint64_t value_count);
void     fs_source_fingerprint_add(FsHash64* h, const int32_t* values, uint64_t value_count);

// =============================================================================
// The frame counts the resample steps make. Writers that scale positions use
// these so their counts agree with the steps.
// =============================================================================
uint32_t fs_source_sinc_frames(uint32_t frames, uint32_t from_rate, uint32_t to_rate);
uint32_t fs_source_cubic_frames(uint32_t frames, uint32_t from_rate, uint32_t to_rate);

// =============================================================================
// The resampler and the conversions of the float steps. FastSampler's export
// calls them directly, so its output and a replay of its steps agree.
// =============================================================================
void    fs_source_cubic_resample_float(const float* src, uint32_t src_frames, uint32_t channels,
                                       uint32_t src_rate, float* dst, uint32_t dst_frames,
                                       uint32_t dst_rate);
float   fs_source_int_to_float(int32_t value, uint32_t bits);
int32_t fs_source_float_to_int(float sample, uint32_t bits);
