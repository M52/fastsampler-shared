// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <pb_encode.h>
#include <pb_decode.h>
#include "fastsampler_formats.h"
#include "fastsampler_hash.h"
#include "fastsampler_source.h"
#include "fastsampler.pb.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); return 1; } } while (0)

// =============================================================================
// Reference copies of the code the steps reproduce: fsbanktool's depth
// conversion and resampler, and FastSampler's export resampler and
// quantization. Keep them as they are.
// =============================================================================

static int32_t ref_convert_depth(int32_t v, int from_bits, int to_bits) {
    if (from_bits == to_bits) return v;
    if (to_bits == 24) return v * 256;
    int32_t r = (v + 128) >> 8;
    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    return r;
}

static double ref_sinc(double x) {
    if (fabs(x) < 1e-12) return 1.0;
    double px = 3.14159265358979323846 * x;
    return sin(px) / px;
}

static uint32_t ref_bank_frames(uint32_t frames, uint32_t source_rate, uint32_t bank_rate) {
    double ratio = (double)bank_rate / (double)source_rate;
    uint32_t out = (uint32_t)floor((double)frames * ratio + 0.5);
    return out == 0 ? 1 : out;
}

static void ref_fsbanktool_resample(const int32_t* scratch, uint32_t src_frames, int src_ch, int src_bits,
                                    uint32_t src_rate, int bank_ch, int bank_bits, uint32_t bank_rate,
                                    uint32_t out_frames, int32_t* out) {
    double in_scale = src_bits > 16 ? 8388608.0 : 32768.0;
    double out_scale = bank_bits == 24 ? 8388608.0 : 32768.0;
    int32_t out_max = bank_bits == 24 ? 8388607 : 32767;
    int32_t out_min = bank_bits == 24 ? -8388608 : -32768;
    double step = (double)src_rate / (double)bank_rate;
    double cutoff = step > 1.0 ? 1.0 / step : 1.0;
    double reach = (double)16 / cutoff;
    for (uint32_t i = 0; i < out_frames; ++i) {
        double t = (double)i * step;
        int64_t first = (int64_t)ceil(t - reach);
        int64_t last = (int64_t)floor(t + reach);
        if (first < 0) first = 0;
        if (last > (int64_t)src_frames - 1) last = (int64_t)src_frames - 1;
        for (int c = 0; c < bank_ch; ++c) {
            int sc = src_ch == 1 ? 0 : c;
            double acc = 0.0;
            for (int64_t n = first; n <= last; ++n) {
                double y = ((double)n - t) * cutoff;
                double w = 0.42 + 0.5 * cos(3.14159265358979323846 * y / 16) +
                           0.08 * cos(2.0 * 3.14159265358979323846 * y / 16);
                acc += (double)scratch[n * src_ch + sc] / in_scale * cutoff * ref_sinc(y) * w;
            }
            int64_t v = (int64_t)floor(acc * out_scale + 0.5);
            if (v > out_max) v = out_max;
            if (v < out_min) v = out_min;
            out[(size_t)i * bank_ch + c] = (int32_t)v;
        }
    }
}

static void ref_resample_buffer_cubic(const float* src, uint32_t src_frames, uint32_t channels, uint32_t src_rate, float* dst, uint32_t dst_frames, uint32_t dst_rate) {
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

static inline int32_t ref_pcm_float_to_int(float sample, float scale, int32_t min_value, int32_t max_value) {
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    int32_t value = (int32_t)lrintf(sample * scale);
    if (value > max_value) value = max_value;
    if (value < min_value) value = min_value;
    return value;
}

// FastSampler's export of one sample: to float, resampled when the rate differs, quantized.
static std::vector<int32_t> ref_fastsampler_export(const std::vector<int32_t>& in, uint32_t frames, uint32_t channels,
                                                   uint32_t in_bits, uint32_t src_rate, uint32_t ref_rate,
                                                   uint32_t bits, uint32_t* out_frames) {
    std::vector<float> original(in.size());
    for (size_t k = 0; k < in.size(); ++k) {
        original[k] = in_bits == 16 ? (float)(int16_t)in[k] * (1.0f / 32768.0f)
                                    : (float)in[k] * (1.0f / 8388608.0f);
    }
    double ratio = (double)ref_rate / (double)src_rate;
    uint32_t count = (uint32_t)round(frames * ratio);
    if (count == 0) count = 1;
    std::vector<float> pcm;
    if (src_rate == ref_rate) {
        pcm = original;
    } else {
        pcm.resize((size_t)count * channels);
        ref_resample_buffer_cubic(original.data(), frames, channels, src_rate, pcm.data(), count, ref_rate);
    }
    float scale = bits == 16 ? 32768.0f : 8388608.0f;
    int32_t lo = bits == 16 ? -32768 : -8388608;
    int32_t hi = bits == 16 ? 32767 : 8388607;
    std::vector<int32_t> out(pcm.size());
    for (size_t k = 0; k < pcm.size(); ++k) out[k] = ref_pcm_float_to_int(pcm[k], scale, lo, hi);
    *out_frames = count;
    return out;
}

// =============================================================================
// Test audio from integer arithmetic only, so it is the same on every machine:
// triangle waves, noise, and both rails.
// =============================================================================
static std::vector<int32_t> make_audio(uint32_t frames, uint32_t channels, uint32_t bits, uint32_t seed) {
    std::vector<int32_t> v((size_t)frames * channels);
    int32_t max_value = bits == 24 ? 8388607 : 32767;
    uint32_t state = seed;
    for (uint32_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < channels; ++c) {
            state = state * 1664525u + 1013904223u;
            int32_t period = 64 + 17 * (int32_t)c;
            int32_t half = period / 2;
            int32_t phase = (int32_t)(f % (uint32_t)period);
            int32_t tri = phase < half ? phase : period - phase;
            int64_t wave = ((int64_t)tri * 2 - half) * (int64_t)max_value / half;
            int64_t s = wave * 3 / 4 + (int64_t)((state >> 8) % 2001) - 1000;
            if (f % 997 == 5) s = max_value;
            if (f % 991 == 7) s = -(int64_t)max_value - 1;
            if (s > max_value) s = max_value;
            if (s < -(int64_t)max_value - 1) s = -(int64_t)max_value - 1;
            v[(size_t)f * channels + c] = (int32_t)s;
        }
    }
    return v;
}

static FsSourceAudio audio_from(const std::vector<int32_t>& values, uint32_t frames, uint32_t channels,
                                uint32_t rate, uint32_t bits) {
    FsSourceAudio a = {};
    a.data = (int32_t*)std::malloc(values.size() * sizeof(int32_t));
    std::memcpy(a.data, values.data(), values.size() * sizeof(int32_t));
    a.frames = frames;
    a.channels = channels;
    a.rate = rate;
    a.bits = bits;
    return a;
}

static bool same_audio(const FsSourceAudio& a, const std::vector<int32_t>& values) {
    return (size_t)a.frames * a.channels == values.size() &&
           std::memcmp(a.data, values.data(), values.size() * sizeof(int32_t)) == 0;
}

static FsSourceStep make_step(uint32_t type, uint32_t rate, uint32_t channels, uint32_t bits,
                              uint32_t first, uint32_t frames) {
    FsSourceStep s = {type, rate, channels, bits, first, frames};
    return s;
}

static uint64_t audio_fingerprint(const FsSourceAudio& a) {
    return fs_source_fingerprint(a.data, (uint64_t)a.frames * a.channels);
}

int main() {
#if defined(_MSC_VER) && defined(_M_X64)
    // The resample steps must give the same values on every x64 machine.
    _set_FMA3_enable(0);
#endif
    char err[256] = "";

    // Published XXH64 values, and the same result over any block sizes.
    CHECK(fs_hash64("", 0) == 0xEF46DB3751D8E999ULL);
    CHECK(fs_hash64("abc", 3) == 0x44BC2CF5AD770999ULL);
    {
        std::vector<uint8_t> bytes(1000);
        uint32_t state = 7;
        for (auto& b : bytes) {
            state = state * 1664525u + 1013904223u;
            b = (uint8_t)(state >> 24);
        }
        uint64_t whole = fs_hash64(bytes.data(), bytes.size());
        const size_t chunks[] = {1, 7, 31, 32, 33, 64, 999};
        for (size_t chunk : chunks) {
            FsHash64 h;
            fs_hash64_begin(&h);
            for (size_t at = 0; at < bytes.size(); at += chunk) {
                size_t n = bytes.size() - at < chunk ? bytes.size() - at : chunk;
                fs_hash64_add(&h, bytes.data() + at, n);
            }
            CHECK(fs_hash64_end(&h) == whole);
        }
        int32_t values[3] = {1, -2, 0x01020304};
        uint8_t le[12] = {1, 0, 0, 0, 0xFE, 0xFF, 0xFF, 0xFF, 4, 3, 2, 1};
        CHECK(fs_source_fingerprint(values, 3) == fs_hash64(le, 12));
    }

    // Channels: mono is copied, the first two of more channels are kept, and
    // nothing else is accepted.
    {
        std::vector<int32_t> mono = make_audio(100, 1, 16, 1);
        FsSourceAudio a = audio_from(mono, 100, 1, 44100, 16);
        FsSourceAudio b = {};
        FsSourceStep stereo = make_step(FS_SOURCE_STEP_CHANNELS, 0, 2, 0, 0, 0);
        CHECK(fs_source_step_apply(&a, &stereo, &b, err, sizeof(err)));
        CHECK(b.channels == 2 && b.frames == 100 && b.bits == 16 && b.rate == 44100);
        for (uint32_t f = 0; f < 100; ++f) CHECK(b.data[f * 2] == mono[f] && b.data[f * 2 + 1] == mono[f]);
        fs_source_audio_free(&b);

        std::vector<int32_t> wide = make_audio(50, 3, 24, 2);
        FsSourceAudio w = audio_from(wide, 50, 3, 44100, 24);
        CHECK(fs_source_step_apply(&w, &stereo, &b, err, sizeof(err)));
        for (uint32_t f = 0; f < 50; ++f) CHECK(b.data[f * 2] == wide[f * 3] && b.data[f * 2 + 1] == wide[f * 3 + 1]);
        FsSourceStep to_mono = make_step(FS_SOURCE_STEP_CHANNELS, 0, 1, 0, 0, 0);
        FsSourceAudio refused = {};
        CHECK(!fs_source_step_apply(&b, &to_mono, &refused, err, sizeof(err)));
        fs_source_audio_free(&a);
        fs_source_audio_free(&b);
        fs_source_audio_free(&w);
    }

    // fsbanktool's depth conversion, against known values and the reference.
    {
        int32_t probe[] = {128, 127, -129, -128, 8388607, -8388608, 383, 384};
        int32_t want[] = {1, 0, -1, 0, 32767, -32768, 1, 2};
        FsSourceAudio p = audio_from(std::vector<int32_t>(probe, probe + 8), 8, 1, 44100, 24);
        FsSourceAudio q = {};
        FsSourceStep to16 = make_step(FS_SOURCE_STEP_DEPTH_ROUND, 0, 0, 16, 0, 0);
        CHECK(fs_source_step_apply(&p, &to16, &q, err, sizeof(err)));
        for (int i = 0; i < 8; ++i) CHECK(q.data[i] == want[i]);
        fs_source_audio_free(&p);
        fs_source_audio_free(&q);

        const uint32_t frames = 3000;
        std::vector<int32_t> src = make_audio(frames, 3, 24, 3);
        std::vector<int32_t> expect((size_t)frames * 2);
        for (uint32_t f = 0; f < frames; ++f) {
            for (int c = 0; c < 2; ++c) expect[(size_t)f * 2 + c] = ref_convert_depth(src[(size_t)f * 3 + c], 24, 16);
        }
        FsSourceStep steps[2] = {make_step(FS_SOURCE_STEP_CHANNELS, 0, 2, 0, 0, 0), to16};
        FsSourceAudio a = audio_from(src, frames, 3, 44100, 24);
        CHECK(fs_source_replay(&a, steps, 2, err, sizeof(err)));
        CHECK(a.bits == 16 && a.channels == 2 && same_audio(a, expect));
        fs_source_audio_free(&a);
    }

    // fsbanktool's resampler: up from mono, down in stereo, and at an uneven ratio.
    {
        struct Case { uint32_t frames, src_ch, src_bits, src_rate, bank_ch, bank_bits, bank_rate, seed; uint64_t fingerprint; };
        const Case cases[] = {
            {1500, 1, 16, 22050, 2, 24, 44100, 4, 0x8f03f352e3f2b4beULL},
            {2000, 2, 24, 48000, 2, 16, 44100, 5, 0x81898fabe7b6199eULL},
            {777, 1, 24, 44100, 1, 24, 48000, 6, 0x90e76482c32208d3ULL},
        };
        for (const Case& k : cases) {
            std::vector<int32_t> src = make_audio(k.frames, k.src_ch, k.src_bits, k.seed);
            uint32_t out_frames = ref_bank_frames(k.frames, k.src_rate, k.bank_rate);
            std::vector<int32_t> expect((size_t)out_frames * k.bank_ch);
            ref_fsbanktool_resample(src.data(), k.frames, (int)k.src_ch, (int)k.src_bits, k.src_rate,
                                    (int)k.bank_ch, (int)k.bank_bits, k.bank_rate, out_frames, expect.data());
            FsSourceStep steps[2];
            uint32_t count = 0;
            if (k.bank_ch != k.src_ch) steps[count++] = make_step(FS_SOURCE_STEP_CHANNELS, 0, k.bank_ch, 0, 0, 0);
            steps[count++] = make_step(FS_SOURCE_STEP_RESAMPLE_SINC, k.bank_rate, 0, k.bank_bits, 0, 0);

            FsSourceAudio shape = {nullptr, k.frames, k.src_ch, k.src_rate, k.src_bits};
            CHECK(fs_source_replay_shape(&shape, steps, count, err, sizeof(err)));
            CHECK(shape.frames == out_frames && shape.channels == k.bank_ch && shape.bits == k.bank_bits);

            FsSourceAudio a = audio_from(src, k.frames, k.src_ch, k.src_rate, k.src_bits);
            CHECK(fs_source_replay(&a, steps, count, err, sizeof(err)));
            CHECK(a.frames == out_frames && a.rate == k.bank_rate && a.bits == k.bank_bits);
            CHECK(same_audio(a, expect));
            CHECK(audio_fingerprint(a) == k.fingerprint);
            fs_source_audio_free(&a);
        }
    }

    // FastSampler's export: resampled at both depths, and quantized without resampling.
    {
        struct Case { uint32_t frames, ch, in_bits, src_rate, ref_rate, bits, seed; uint32_t type; uint64_t fingerprint; };
        const Case cases[] = {
            {1200, 2, 16, 44100, 48000, 24, 7, FS_SOURCE_STEP_RESAMPLE_CUBIC, 0xf2703aadc7f208aeULL},
            {1000, 1, 24, 48000, 44100, 16, 8, FS_SOURCE_STEP_RESAMPLE_CUBIC, 0xb0c019a913e8c100ULL},
            {900, 2, 24, 44100, 44100, 16, 9, FS_SOURCE_STEP_DEPTH_FLOAT, 0xb1d2fb820cd47088ULL},
        };
        for (const Case& k : cases) {
            std::vector<int32_t> src = make_audio(k.frames, k.ch, k.in_bits, k.seed);
            uint32_t out_frames = 0;
            std::vector<int32_t> expect = ref_fastsampler_export(src, k.frames, k.ch, k.in_bits, k.src_rate,
                                                                 k.ref_rate, k.bits, &out_frames);
            FsSourceStep s = k.type == FS_SOURCE_STEP_DEPTH_FLOAT
                                 ? make_step(FS_SOURCE_STEP_DEPTH_FLOAT, 0, 0, k.bits, 0, 0)
                                 : make_step(FS_SOURCE_STEP_RESAMPLE_CUBIC, k.ref_rate, 0, k.bits, 0, 0);
            FsSourceAudio a = audio_from(src, k.frames, k.ch, k.src_rate, k.in_bits);
            CHECK(fs_source_replay(&a, &s, 1, err, sizeof(err)));
            CHECK(a.frames == out_frames && a.bits == k.bits);
            CHECK(same_audio(a, expect));
            CHECK(audio_fingerprint(a) == k.fingerprint);
            fs_source_audio_free(&a);
        }

        int32_t probe[] = {128, 384, 383, -128, -384, 8388607, -8388608, 640};
        int32_t want[] = {0, 2, 1, 0, -2, 32767, -32768, 2};
        FsSourceAudio p = audio_from(std::vector<int32_t>(probe, probe + 8), 8, 1, 44100, 24);
        FsSourceAudio q = {};
        FsSourceStep to16 = make_step(FS_SOURCE_STEP_DEPTH_FLOAT, 0, 0, 16, 0, 0);
        CHECK(fs_source_step_apply(&p, &to16, &q, err, sizeof(err)));
        for (int i = 0; i < 8; ++i) CHECK(q.data[i] == want[i]);
        fs_source_audio_free(&p);
        fs_source_audio_free(&q);
    }

    // Cut, and the steps that are refused.
    {
        std::vector<int32_t> src = make_audio(500, 2, 16, 10);
        FsSourceAudio a = audio_from(src, 500, 2, 44100, 16);
        FsSourceAudio b = {};
        FsSourceStep cut = make_step(FS_SOURCE_STEP_CUT, 0, 0, 0, 100, 250);
        CHECK(fs_source_step_apply(&a, &cut, &b, err, sizeof(err)));
        CHECK(b.frames == 250 && std::memcmp(b.data, src.data() + 200, 500 * sizeof(int32_t)) == 0);
        FsSourceAudio refused = {};
        FsSourceStep past = make_step(FS_SOURCE_STEP_CUT, 0, 0, 0, 400, 101);
        CHECK(!fs_source_step_apply(&a, &past, &refused, err, sizeof(err)));
        FsSourceStep empty = make_step(FS_SOURCE_STEP_CUT, 0, 0, 0, 0, 0);
        CHECK(!fs_source_step_apply(&a, &empty, &refused, err, sizeof(err)));
        FsSourceStep unknown = make_step(99, 0, 0, 0, 0, 0);
        CHECK(!fs_source_step_apply(&a, &unknown, &refused, err, sizeof(err)));
        FsSourceStep no_rate = make_step(FS_SOURCE_STEP_RESAMPLE_CUBIC, 0, 0, 24, 0, 0);
        CHECK(!fs_source_step_apply(&a, &no_rate, &refused, err, sizeof(err)));
        FsSourceStep steps[2] = {cut, unknown};
        CHECK(!fs_source_replay(&a, steps, 2, err, sizeof(err)));
        CHECK(std::strncmp(err, "Step 2:", 7) == 0);
        CHECK(a.frames == 250);
        fs_source_audio_free(&a);
        fs_source_audio_free(&b);
    }

    // The source records survive encoding and decoding, with their field numbers.
    {
        CHECK(FSIFile_source_files_tag == 15 && FSIFile_source_samples_tag == 16);
        CHECK(FSIFile_bank_sample_rate_tag == 17 && FSIFile_bank_channels_tag == 18);
        CHECK(FSIZone_source_sample_tag == 32 && FSIZone_source_first_tag == 33 && FSIZone_source_frames_tag == 34);
        CHECK(FSISourceSample_steps_tag == 4 && FSISourceSample_result_fingerprint_tag == 7);
        CHECK(FS_SOURCE_MAX_FILES == 256 && FS_SOURCE_MAX_SAMPLES == 4096 && FS_SOURCE_MAX_STEPS == 8);

        auto written = std::make_unique<FSIFile>();
        auto decoded = std::make_unique<FSIFile>();
        std::memset(written.get(), 0, sizeof(FSIFile));
        std::memset(decoded.get(), 0, sizeof(FSIFile));
        CHECK(sizeof(written->source_files) / sizeof(written->source_files[0]) == FS_SOURCE_MAX_FILES);
        CHECK(sizeof(written->source_samples) / sizeof(written->source_samples[0]) == FS_SOURCE_MAX_SAMPLES);
        CHECK(sizeof(written->source_samples[0].steps) / sizeof(written->source_samples[0].steps[0]) == FS_SOURCE_MAX_STEPS);
        CHECK(sizeof(written->source_files[0].name) == FS_SOURCE_NAME_LENGTH);

        std::strcpy(written->instrument_name, "Violins");
        std::strcpy(written->fsb_path, "Violins.fsb");
        written->zones_count = 1;
        FSIZone& zone = written->zones[0];
        std::strcpy(zone.name, "C4");
        zone.has_source_sample = true;
        zone.source_sample = 1;
        zone.has_source_first = true;
        zone.source_first = 10;
        zone.has_source_frames = true;
        zone.source_frames = 20;

        written->source_files_count = 2;
        FSISourceFile& gig = written->source_files[0];
        gig.has_name = true;
        std::strcpy(gig.name, "Violins P.gig");
        gig.has_folder = true;
        std::strcpy(gig.folder, "Violins");
        gig.has_size = true;
        gig.size = 123456789012ULL;
        gig.has_hash = true;
        gig.hash = 0xFEDCBA9876543210ULL;
        FSISourceFile& part = written->source_files[1];
        part.has_name = true;
        std::strcpy(part.name, "Violins P.gx01");
        part.has_extension_of = true;
        part.extension_of = 1;

        written->source_samples_count = 1;
        FSISourceSample& sample = written->source_samples[0];
        sample.has_file = true;
        sample.file = 0;
        sample.has_sample = true;
        sample.sample = 57;
        sample.has_source_fingerprint = true;
        sample.source_fingerprint = 0x0123456789ABCDEFULL;
        sample.steps_count = 3;
        sample.steps[0].has_type = true;
        sample.steps[0].type = FS_SOURCE_STEP_CHANNELS;
        sample.steps[0].has_channels = true;
        sample.steps[0].channels = 2;
        sample.steps[1].has_type = true;
        sample.steps[1].type = FS_SOURCE_STEP_RESAMPLE_SINC;
        sample.steps[1].has_rate = true;
        sample.steps[1].rate = 44100;
        sample.steps[1].has_bits = true;
        sample.steps[1].bits = 24;
        sample.steps[2].has_type = true;
        sample.steps[2].type = FS_SOURCE_STEP_CUT;
        sample.steps[2].has_first = true;
        sample.steps[2].first = 5;
        sample.steps[2].has_frames = true;
        sample.steps[2].frames = 100;
        sample.has_bits = true;
        sample.bits = 24;
        sample.has_frames = true;
        sample.frames = 100;
        sample.has_result_fingerprint = true;
        sample.result_fingerprint = 0x8000000000000001ULL;
        written->has_bank_sample_rate = true;
        written->bank_sample_rate = 44100;
        written->has_bank_channels = true;
        written->bank_channels = 2;

        std::vector<uint8_t> buffer(1 << 16);
        pb_ostream_t output = pb_ostream_from_buffer(buffer.data(), buffer.size());
        CHECK(pb_encode(&output, FSIFile_fields, written.get()));
        pb_istream_t input = pb_istream_from_buffer(buffer.data(), output.bytes_written);
        CHECK(pb_decode(&input, FSIFile_fields, decoded.get()));

        const FSIZone& z = decoded->zones[0];
        CHECK(decoded->zones_count == 1 && z.has_source_sample && z.source_sample == 1);
        CHECK(z.has_source_first && z.source_first == 10 && z.has_source_frames && z.source_frames == 20);
        CHECK(decoded->source_files_count == 2);
        CHECK(std::strcmp(decoded->source_files[0].name, "Violins P.gig") == 0);
        CHECK(std::strcmp(decoded->source_files[0].folder, "Violins") == 0);
        CHECK(decoded->source_files[0].size == 123456789012ULL && decoded->source_files[0].hash == 0xFEDCBA9876543210ULL);
        CHECK(!decoded->source_files[0].has_extension_of);
        CHECK(decoded->source_files[1].has_extension_of && decoded->source_files[1].extension_of == 1);
        const FSISourceSample& s = decoded->source_samples[0];
        CHECK(decoded->source_samples_count == 1 && s.has_file && s.file == 0 && s.sample == 57);
        CHECK(s.source_fingerprint == 0x0123456789ABCDEFULL && s.result_fingerprint == 0x8000000000000001ULL);
        CHECK(s.steps_count == 3 && s.steps[1].type == FS_SOURCE_STEP_RESAMPLE_SINC && s.steps[1].rate == 44100);
        CHECK(s.steps[2].type == FS_SOURCE_STEP_CUT && s.steps[2].first == 5 && s.steps[2].frames == 100);
        CHECK(s.bits == 24 && s.frames == 100);
        CHECK(decoded->has_bank_sample_rate && decoded->bank_sample_rate == 44100);
        CHECK(decoded->has_bank_channels && decoded->bank_channels == 2);
    }

    std::puts("All source checks passed.");
    return 0;
}
