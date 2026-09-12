// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_source.cpp
// Created on: 2026-09-13
// Apply the processing steps that make stored samples from source audio.
// =============================================================================

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../include/fastsampler_hash.h"
#include "../include/fastsampler_source.h"

// =============================================================================
// These constants are part of the persisted step definitions. Do not change
// them.
// =============================================================================
#define FS_SOURCE_SINC_HALF_WIDTH   16
#define FS_SOURCE_PI                3.14159265358979323846
#define FS_SOURCE_SCALE_16          32768.0
#define FS_SOURCE_SCALE_24          8388608.0
#define FS_SOURCE_MAX_16            32767
#define FS_SOURCE_MIN_16            (-32768)
#define FS_SOURCE_MAX_24            8388607
#define FS_SOURCE_MIN_24            (-8388608)

static bool fs_source_fail(char* err, size_t err_len, const char* message) {
    if (err && err_len) snprintf(err, err_len, "%s", message);
    return false;
}

static bool fs_source_depth_ok(uint32_t bits) {
    return bits == 16 || bits == 24;
}

static int32_t fs_source_max_value(uint32_t bits) {
    return bits == 24 ? FS_SOURCE_MAX_24 : FS_SOURCE_MAX_16;
}

static int32_t fs_source_min_value(uint32_t bits) {
    return bits == 24 ? FS_SOURCE_MIN_24 : FS_SOURCE_MIN_16;
}

void fs_source_audio_free(FsSourceAudio* audio) {
    if (!audio) return;
    free(audio->data);
    memset(audio, 0, sizeof(*audio));
}

uint32_t fs_source_sinc_frames(uint32_t frames, uint32_t from_rate, uint32_t to_rate) {
    double ratio = (double)to_rate / (double)from_rate;
    uint32_t out = (uint32_t)floor((double)frames * ratio + 0.5);
    return out == 0 ? 1 : out;
}

uint32_t fs_source_cubic_frames(uint32_t frames, uint32_t from_rate, uint32_t to_rate) {
    double ratio = (double)to_rate / (double)from_rate;
    uint32_t out = (uint32_t)round(frames * ratio);
    return out == 0 ? 1 : out;
}

bool fs_source_step_shape(const FsSourceAudio* in, const FsSourceStep* step, FsSourceAudio* out,
                          char* err, size_t err_len) {
    if (!fs_source_depth_ok(in->bits) || in->channels == 0 || in->frames == 0 || in->rate == 0)
        return fs_source_fail(err, err_len, "The audio before the step is not valid");
    *out = *in;
    out->data = nullptr;

    switch (step->type) {
    case FS_SOURCE_STEP_CHANNELS:
        if (step->channels == in->channels) return true;
        if (step->channels != 2)
            return fs_source_fail(err, err_len, "A channels step can only make stereo from mono or from more channels");
        out->channels = 2;
        return true;

    case FS_SOURCE_STEP_DEPTH_ROUND:
    case FS_SOURCE_STEP_DEPTH_FLOAT:
        if (!fs_source_depth_ok(step->bits))
            return fs_source_fail(err, err_len, "A depth step needs 16 or 24 bits");
        out->bits = step->bits;
        return true;

    case FS_SOURCE_STEP_RESAMPLE_SINC:
    case FS_SOURCE_STEP_RESAMPLE_CUBIC: {
        if (!fs_source_depth_ok(step->bits) || step->rate == 0)
            return fs_source_fail(err, err_len, "A resample step needs a rate and 16 or 24 bits");
        double estimate = (double)in->frames * ((double)step->rate / (double)in->rate) + 0.5;
        if (estimate >= 4294967295.0)
            return fs_source_fail(err, err_len, "The resampled sample is too long");
        out->frames = step->type == FS_SOURCE_STEP_RESAMPLE_SINC
                          ? fs_source_sinc_frames(in->frames, in->rate, step->rate)
                          : fs_source_cubic_frames(in->frames, in->rate, step->rate);
        out->rate = step->rate;
        out->bits = step->bits;
        return true;
    }

    case FS_SOURCE_STEP_CUT:
        if (step->frames == 0 || step->first >= in->frames || step->frames > in->frames - step->first)
            return fs_source_fail(err, err_len, "A cut step reaches past the end of the sample");
        out->frames = step->frames;
        return true;

    default:
        if (err && err_len) snprintf(err, err_len, "Unknown step type %u", step->type);
        return false;
    }
}

bool fs_source_replay_shape(FsSourceAudio* shape, const FsSourceStep* steps, uint32_t step_count,
                            char* err, size_t err_len) {
    for (uint32_t s = 0; s < step_count; ++s) {
        FsSourceAudio next;
        char why[192] = "";
        if (!fs_source_step_shape(shape, &steps[s], &next, why, sizeof(why))) {
            if (err && err_len) snprintf(err, err_len, "Step %u: %s", s + 1, why);
            return false;
        }
        *shape = next;
    }
    return true;
}

// =============================================================================
// The step implementations. Each one fills out, whose format
// fs_source_step_shape() has already set.
// =============================================================================

static void fs_source_channels(const FsSourceAudio* in, FsSourceAudio* out) {
    for (uint32_t f = 0; f < out->frames; ++f) {
        for (uint32_t c = 0; c < out->channels; ++c) {
            uint32_t sc = in->channels == 1 ? 0 : c;
            out->data[(size_t)f * out->channels + c] = in->data[(size_t)f * in->channels + sc];
        }
    }
}

static int32_t fs_source_round_depth(int32_t v, uint32_t from_bits, uint32_t to_bits) {
    if (from_bits == to_bits) return v;
    if (to_bits == 24) return v * 256;
    int32_t r = (v + 128) >> 8;
    if (r > FS_SOURCE_MAX_16) r = FS_SOURCE_MAX_16;
    if (r < FS_SOURCE_MIN_16) r = FS_SOURCE_MIN_16;
    return r;
}

static void fs_source_depth_round(const FsSourceAudio* in, FsSourceAudio* out) {
    size_t count = (size_t)out->frames * out->channels;
    for (size_t k = 0; k < count; ++k) out->data[k] = fs_source_round_depth(in->data[k], in->bits, out->bits);
}

float fs_source_int_to_float(int32_t value, uint32_t bits) {
    return (float)value * (bits == 24 ? (1.0f / 8388608.0f) : (1.0f / 32768.0f));
}

// =============================================================================
// Round to the nearest integer with lrintf, as FastSampler's export does.
// Clamp before scaling and after rounding to protect the positive limit.
// =============================================================================
int32_t fs_source_float_to_int(float sample, uint32_t bits) {
    float scale = bits == 24 ? 8388608.0f : 32768.0f;
    int32_t max_value = fs_source_max_value(bits);
    int32_t min_value = fs_source_min_value(bits);
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    int32_t value = (int32_t)lrintf(sample * scale);
    if (value > max_value) value = max_value;
    if (value < min_value) value = min_value;
    return value;
}

static void fs_source_depth_float(const FsSourceAudio* in, FsSourceAudio* out) {
    size_t count = (size_t)out->frames * out->channels;
    for (size_t k = 0; k < count; ++k)
        out->data[k] = fs_source_float_to_int(fs_source_int_to_float(in->data[k], in->bits), out->bits);
}

static double fs_source_sinc_value(double x) {
    if (fabs(x) < 1e-12) return 1.0;
    double px = FS_SOURCE_PI * x;
    return sin(px) / px;
}

// =============================================================================
// fsbanktool's resampler: a windowed sinc with this many zero crossings each
// side and a Blackman window. Going down in rate, the filter narrows so that
// nothing above the new Nyquist frequency folds back. Frames outside the
// sample count as zero.
// =============================================================================
static void fs_source_sinc(const FsSourceAudio* in, FsSourceAudio* out) {
    double in_scale = in->bits == 24 ? FS_SOURCE_SCALE_24 : FS_SOURCE_SCALE_16;
    double out_scale = out->bits == 24 ? FS_SOURCE_SCALE_24 : FS_SOURCE_SCALE_16;
    int32_t out_max = fs_source_max_value(out->bits);
    int32_t out_min = fs_source_min_value(out->bits);
    double step = (double)in->rate / (double)out->rate;
    double cutoff = step > 1.0 ? 1.0 / step : 1.0;
    double reach = (double)FS_SOURCE_SINC_HALF_WIDTH / cutoff;
    uint32_t ch = out->channels;

    for (uint32_t i = 0; i < out->frames; ++i) {
        double t = (double)i * step;
        int64_t first = (int64_t)ceil(t - reach);
        int64_t last = (int64_t)floor(t + reach);
        if (first < 0) first = 0;
        if (last > (int64_t)in->frames - 1) last = (int64_t)in->frames - 1;
        for (uint32_t c = 0; c < ch; ++c) {
            double acc = 0.0;
            for (int64_t n = first; n <= last; ++n) {
                double y = ((double)n - t) * cutoff;
                double w = 0.42 + 0.5 * cos(FS_SOURCE_PI * y / FS_SOURCE_SINC_HALF_WIDTH) +
                           0.08 * cos(2.0 * FS_SOURCE_PI * y / FS_SOURCE_SINC_HALF_WIDTH);
                acc += (double)in->data[n * ch + c] / in_scale * cutoff * fs_source_sinc_value(y) * w;
            }
            int64_t v = (int64_t)floor(acc * out_scale + 0.5);
            if (v > out_max) v = out_max;
            if (v < out_min) v = out_min;
            out->data[(size_t)i * ch + c] = (int32_t)v;
        }
    }
}

void fs_source_cubic_resample_float(const float* src, uint32_t src_frames, uint32_t channels,
                                    uint32_t src_rate, float* dst, uint32_t dst_frames,
                                    uint32_t dst_rate) {
#if defined(__clang__)
    // A fused multiply-add rounds differently, and the result must be the
    // same on every platform.
    #pragma clang fp contract(off)
#endif
    if (src_frames == 0 || dst_frames == 0) return;
    double ratio = (double)src_rate / (double)dst_rate;

    for (uint32_t f = 0; f < dst_frames; ++f) {
        double src_time = f * ratio;
        int64_t int_idx = (int64_t)floor(src_time);
        float t = (float)(src_time - int_idx);

        int64_t idx[4] = { int_idx - 1, int_idx, int_idx + 1, int_idx + 2 };
        for (int k = 0; k < 4; ++k) {
            if (idx[k] < 0) idx[k] = 0;
            if (idx[k] >= (int64_t)src_frames) idx[k] = src_frames - 1;
        }

        for (uint32_t ch = 0; ch < channels; ++ch) {
            float y0 = src[idx[0] * channels + ch];
            float y1 = src[idx[1] * channels + ch];
            float y2 = src[idx[2] * channels + ch];
            float y3 = src[idx[3] * channels + ch];

            float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
            float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float c = -0.5f * y0 + 0.5f * y2;
            dst[f * channels + ch] = ((a * t + b) * t + c) * t + y1;
        }
    }
}

static bool fs_source_cubic(const FsSourceAudio* in, FsSourceAudio* out, char* err, size_t err_len) {
    size_t in_values = (size_t)in->frames * in->channels;
    size_t out_values = (size_t)out->frames * out->channels;
    float* src = (float*)malloc(in_values * sizeof(float));
    float* dst = (float*)malloc(out_values * sizeof(float));
    if (!src || !dst) {
        free(src);
        free(dst);
        return fs_source_fail(err, err_len, "Out of memory for a resample step");
    }
    for (size_t k = 0; k < in_values; ++k) src[k] = fs_source_int_to_float(in->data[k], in->bits);
    fs_source_cubic_resample_float(src, in->frames, in->channels, in->rate, dst, out->frames, out->rate);
    for (size_t k = 0; k < out_values; ++k) out->data[k] = fs_source_float_to_int(dst[k], out->bits);
    free(src);
    free(dst);
    return true;
}

bool fs_source_step_apply(const FsSourceAudio* in, const FsSourceStep* step, FsSourceAudio* out,
                          char* err, size_t err_len) {
    FsSourceAudio shape;
    if (!fs_source_step_shape(in, step, &shape, err, err_len)) return false;
    if (!in->data) return fs_source_fail(err, err_len, "The audio before the step has no data");
    uint64_t values = (uint64_t)shape.frames * shape.channels;
    if (values > (uint64_t)(SIZE_MAX / sizeof(float)))
        return fs_source_fail(err, err_len, "The sample is too large for this build");
    shape.data = (int32_t*)malloc((size_t)values * sizeof(int32_t));
    if (!shape.data) return fs_source_fail(err, err_len, "Out of memory for a processing step");

    bool ok = true;
    switch (step->type) {
    case FS_SOURCE_STEP_CHANNELS:
        fs_source_channels(in, &shape);
        break;
    case FS_SOURCE_STEP_DEPTH_ROUND:
        fs_source_depth_round(in, &shape);
        break;
    case FS_SOURCE_STEP_DEPTH_FLOAT:
        fs_source_depth_float(in, &shape);
        break;
    case FS_SOURCE_STEP_RESAMPLE_SINC:
        fs_source_sinc(in, &shape);
        break;
    case FS_SOURCE_STEP_RESAMPLE_CUBIC:
        ok = fs_source_cubic(in, &shape, err, err_len);
        break;
    case FS_SOURCE_STEP_CUT:
        memcpy(shape.data, in->data + (size_t)step->first * in->channels, (size_t)values * sizeof(int32_t));
        break;
    }
    if (!ok) {
        free(shape.data);
        return false;
    }
    *out = shape;
    return true;
}

bool fs_source_replay(FsSourceAudio* audio, const FsSourceStep* steps, uint32_t step_count,
                      char* err, size_t err_len) {
    for (uint32_t s = 0; s < step_count; ++s) {
        FsSourceAudio next;
        char why[192] = "";
        if (!fs_source_step_apply(audio, &steps[s], &next, why, sizeof(why))) {
            if (err && err_len) snprintf(err, err_len, "Step %u: %s", s + 1, why);
            return false;
        }
        fs_source_audio_free(audio);
        *audio = next;
    }
    return true;
}

void fs_source_fingerprint_add(FsHash64* h, const int32_t* values, uint64_t value_count) {
    uint8_t block[4096];
    size_t used = 0;
    for (uint64_t i = 0; i < value_count; ++i) {
        uint32_t v = (uint32_t)values[i];
        block[used] = (uint8_t)v;
        block[used + 1] = (uint8_t)(v >> 8);
        block[used + 2] = (uint8_t)(v >> 16);
        block[used + 3] = (uint8_t)(v >> 24);
        used += 4;
        if (used == sizeof(block)) {
            fs_hash64_add(h, block, used);
            used = 0;
        }
    }
    if (used) fs_hash64_add(h, block, used);
}

uint64_t fs_source_fingerprint(const int32_t* values, uint64_t value_count) {
    FsHash64 h;
    fs_hash64_begin(&h);
    fs_source_fingerprint_add(&h, values, value_count);
    return fs_hash64_end(&h);
}
