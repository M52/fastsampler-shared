// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "fastsampler_smfp.h"
#include "fastsampler_smfp_analysis.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); return 1; } } while (0)

// =============================================================================
// Reference copies of the analysis as FastSampler ran it before it moved
// here: fastsampler_fft(), smf_resolve_zone_range(), smf_analyse_zone_bands(),
// and the root note conversion that FastSampler and FastResampler both used.
// Stored fingerprints were measured with them. Keep them as they are.
// =============================================================================

#define REF_FFT_BITS          12
#define REF_FFT_SIZE          (1 << REF_FFT_BITS)
#define REF_BINS              (REF_FFT_SIZE / 2 + 1)
#define REF_HOP               (REF_FFT_SIZE / 4)
#define REF_PI                3.14159265358979323846f
#define REF_TWO_PI            (2.0f * REF_PI)
#define REF_RANGE_START       0.15f
#define REF_RANGE_END         0.85f
#define REF_NEPER_TO_DB       8.685889638f
#define REF_LOG_FLOOR_NEPERS  9.2103404f
#define REF_MIN_HALF_BINS     2
#define REF_MAX_HALF_BINS     256
#define REF_BAND_DB_MIN       (-120.0f)
#define REF_BAND_DB_MAX       60.0f

struct RefScratch {
    float real[REF_FFT_SIZE];
    float imag[REF_FFT_SIZE];
    float window[REF_FFT_SIZE];
    float power[REF_BINS];
    float log_mag[REF_BINS];
};

static unsigned int ref_bit_reverse(unsigned int x, int bits) {
    unsigned int y = 0;
    for (int i = 0; i < bits; ++i) {
        y = (y << 1) | (x & 1);
        x >>= 1;
    }
    return y;
}

static void ref_fft(float* real, float* imag, int bits) {
    int N = 1 << bits;

    for (int i = 0; i < N; ++i) {
        int rev = (int)ref_bit_reverse((unsigned int)i, bits);
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
        float w_r = cosf(-REF_TWO_PI / m);
        float w_i = sinf(-REF_TWO_PI / m);

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

static void ref_scratch_init(RefScratch* s) {
    for (int i = 0; i < REF_FFT_SIZE; ++i) {
        s->window[i] = 0.5f * (1.0f - cosf(REF_TWO_PI * (float)i / (float)(REF_FFT_SIZE - 1)));
    }
}

static void ref_resolve_zone_range(uint32_t total_frames, uint32_t play_start, uint32_t play_end,
                                   bool loop_enabled, uint32_t loop_start, uint32_t loop_end,
                                   bool manual, float start_frac, float end_frac,
                                   uint32_t* out_start, uint32_t* out_end) {
    uint32_t p_start = play_start;
    uint32_t p_end = (play_end == 0 || play_end > total_frames) ? total_frames : play_end;
    if (p_start >= p_end) {
        p_start = 0;
        p_end = total_frames;
    }

    uint32_t s, e;
    bool use_loop = !manual && loop_enabled && loop_end > loop_start && loop_end <= total_frames;
    if (use_loop) {
        s = loop_start;
        e = loop_end;
    } else {
        float sf = start_frac;
        float ef = end_frac;
        if (sf < 0.0f) sf = 0.0f;
        if (sf > 1.0f) sf = 1.0f;
        if (ef < 0.0f) ef = 0.0f;
        if (ef > 1.0f) ef = 1.0f;
        if (ef <= sf) {
            sf = REF_RANGE_START;
            ef = REF_RANGE_END;
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

    if (e - s < (uint32_t)REF_FFT_SIZE) {
        s = p_start;
        e = p_end;
    }

    *out_start = s;
    *out_end = e;
}

static float ref_root_note_hz(int root_note) {
    return 440.0f * exp2f(((float)root_note - (float)69) / (float)12);
}

static uint32_t ref_analyse_zone_bands(RefScratch* scratch, const float* pcm, uint32_t total_frames,
                                       uint32_t channels, uint32_t sample_rate,
                                       uint32_t range_start, uint32_t range_end,
                                       float f0_hz, float* out_band_db) {
    for (int b = 0; b < SMFP_BAND_COUNT; ++b) out_band_db[b] = 0.0f;

    if (!pcm || channels == 0 || sample_rate == 0) return 0;
    if (range_end > total_frames) range_end = total_frames;
    if (range_start >= range_end) return 0;
    if (range_end - range_start < (uint32_t)REF_FFT_SIZE) return 0;

    const int N = REF_FFT_SIZE;

    memset(scratch->power, 0, sizeof(scratch->power));
    uint32_t window_count = 0;

    for (uint32_t pos = range_start; pos + (uint32_t)N <= range_end; pos += REF_HOP) {
        for (uint32_t c = 0; c < channels; ++c) {
            const float* src = pcm + (size_t)pos * channels + c;
            for (int i = 0; i < N; ++i) {
                scratch->real[i] = src[(size_t)i * channels] * scratch->window[i];
            }
            memset(scratch->imag, 0, sizeof(scratch->imag));

            ref_fft(scratch->real, scratch->imag, REF_FFT_BITS);

            for (int k = 0; k < REF_BINS; ++k) {
                scratch->power[k] += scratch->real[k] * scratch->real[k] +
                                     scratch->imag[k] * scratch->imag[k];
            }
        }
        window_count++;
    }

    if (window_count == 0) return 0;

    float inv = 1.0f / (float)(window_count * channels);
    float peak_log = -1e30f;
    for (int k = 0; k < REF_BINS; ++k) {
        float v = 0.5f * logf(scratch->power[k] * inv + 1e-20f);
        scratch->log_mag[k] = v;
        if (v > peak_log) peak_log = v;
    }

    float floor_log = peak_log - REF_LOG_FLOOR_NEPERS;
    for (int k = 0; k < REF_BINS; ++k) {
        if (scratch->log_mag[k] < floor_log) scratch->log_mag[k] = floor_log;
    }

    int half = REF_MIN_HALF_BINS;
    if (f0_hz > 1.0f) half = (int)(0.5f * f0_hz * (float)N / (float)sample_rate);
    if (half < REF_MIN_HALF_BINS) half = REF_MIN_HALF_BINS;
    if (half > REF_MAX_HALF_BINS) half = REF_MAX_HALF_BINS;

    for (int k = 0; k < REF_BINS; ++k) {
        int lo = k - half; if (lo < 0) lo = 0;
        int hi = k + half; if (hi > REF_BINS - 1) hi = REF_BINS - 1;

        float peak = scratch->log_mag[lo];
        for (int j = lo + 1; j <= hi; ++j) {
            if (scratch->log_mag[j] > peak) peak = scratch->log_mag[j];
        }
        scratch->real[k] = peak;
    }

    float run = 0.0f;
    for (int k = 0; k <= half && k < REF_BINS; ++k) run += scratch->real[k];
    int count = (half + 1 < REF_BINS) ? half + 1 : REF_BINS;

    for (int k = 0; k < REF_BINS; ++k) {
        scratch->log_mag[k] = run / (float)count;

        int add = k + half + 1;
        int drop = k - half;
        if (add < REF_BINS) { run += scratch->real[add]; count++; }
        if (drop >= 0) { run -= scratch->real[drop]; count--; }
    }

    float bins_per_hz = (float)N / (float)sample_rate;
    float max_bin = (float)(REF_BINS - 1);

    for (int b = 0; b < SMFP_BAND_COUNT; ++b) {
        float bin = smfp_band_center_hz(b) * bins_per_hz;
        if (bin < 0.0f) bin = 0.0f;
        if (bin > max_bin) bin = max_bin;

        int i0 = (int)bin;
        int i1 = i0 + 1;
        if (i1 > REF_BINS - 1) i1 = REF_BINS - 1;
        float t = bin - (float)i0;

        float v = scratch->log_mag[i0] * (1.0f - t) + scratch->log_mag[i1] * t;
        out_band_db[b] = v * REF_NEPER_TO_DB;
    }

    double total_power = 0.0;
    for (int b = 0; b < SMFP_BAND_COUNT; ++b) {
        total_power += pow(10.0, (double)out_band_db[b] * 0.1);
    }
    if (total_power > 0.0) {
        float mean_db = (float)(10.0 * log10(total_power / (double)SMFP_BAND_COUNT));
        for (int b = 0; b < SMFP_BAND_COUNT; ++b) out_band_db[b] -= mean_db;
    }

    for (int b = 0; b < SMFP_BAND_COUNT; ++b) {
        if (out_band_db[b] < REF_BAND_DB_MIN) out_band_db[b] = REF_BAND_DB_MIN;
        if (out_band_db[b] > REF_BAND_DB_MAX) out_band_db[b] = REF_BAND_DB_MAX;
    }

    return window_count * REF_HOP;
}

// =============================================================================
// Test signals.
// =============================================================================

static uint32_t next_random(uint32_t* state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float next_noise(uint32_t* state) {
    return (float)(next_random(state) >> 8) / 8388608.0f - 1.0f;
}

// =============================================================================
// A harmonic tone whose partials fall db_per_octave, peaking at 0.9. Each
// further channel falls 3 dB per octave more, and noise_level adds noise.
// =============================================================================
static std::vector<float> make_tone(uint32_t frames, uint32_t channels, uint32_t sample_rate,
                                    float f0, float db_per_octave, float noise_level) {
    std::vector<float> pcm((size_t)frames * channels, 0.0f);
    uint32_t seed = 12345;
    for (uint32_t c = 0; c < channels; ++c) {
        float slope = db_per_octave - 3.0f * (float)c;
        for (int h = 1; h * f0 < (float)sample_rate * 0.45f; ++h) {
            float freq = f0 * (float)h;
            float amp = powf(10.0f, (slope * log2f(freq / f0)) / 20.0f);
            float phase = (float)h * 0.7f;
            float w = REF_TWO_PI * freq / (float)sample_rate;
            for (uint32_t i = 0; i < frames; ++i) {
                pcm[(size_t)i * channels + c] += amp * sinf(w * (float)i + phase);
            }
        }
        for (uint32_t i = 0; i < frames; ++i) {
            pcm[(size_t)i * channels + c] += noise_level * next_noise(&seed);
        }
    }

    float peak = 0.0f;
    for (float v : pcm) {
        if (fabsf(v) > peak) peak = fabsf(v);
    }
    if (peak > 0.0f) {
        for (float& v : pcm) v *= 0.9f / peak;
    }
    return pcm;
}

static float band_spread(const float* bands, int first, int last) {
    float spread = 0.0f;
    for (int b = first; b <= last; ++b) {
        float d = fabsf(bands[b] - bands[first]);
        if (d > spread) spread = d;
    }
    return spread;
}

int main() {
    // =============================================================================
    // The FFT against a direct sum, and against the reference bit for bit.
    // =============================================================================
    {
        const int bits = 6;
        const int n = 1 << bits;
        float real[64], imag[64];
        double x[64];
        uint32_t seed = 1;
        for (int i = 0; i < n; ++i) {
            x[i] = next_noise(&seed);
            real[i] = (float)x[i];
            imag[i] = 0.0f;
        }
        smfp_fft(real, imag, bits);
        for (int k = 0; k < n; ++k) {
            double sum_r = 0.0, sum_i = 0.0;
            for (int i = 0; i < n; ++i) {
                double a = -2.0 * 3.14159265358979323846 * k * i / n;
                sum_r += x[i] * cos(a);
                sum_i += x[i] * sin(a);
            }
            CHECK(fabs(real[k] - sum_r) < 1e-3 && fabs(imag[k] - sum_i) < 1e-3);
        }

        std::vector<float> a_real(REF_FFT_SIZE), a_imag(REF_FFT_SIZE), b_real(REF_FFT_SIZE), b_imag(REF_FFT_SIZE);
        for (int i = 0; i < REF_FFT_SIZE; ++i) {
            a_real[i] = b_real[i] = next_noise(&seed);
            a_imag[i] = b_imag[i] = next_noise(&seed);
        }
        smfp_fft(a_real.data(), a_imag.data(), REF_FFT_BITS);
        ref_fft(b_real.data(), b_imag.data(), REF_FFT_BITS);
        CHECK(a_real == b_real && a_imag == b_imag);
    }

    auto scratch = std::make_unique<SMFPAnalysisScratch>();
    auto ref = std::make_unique<RefScratch>();
    smfp_analysis_scratch_init(scratch.get());
    ref_scratch_init(ref.get());
    CHECK(std::memcmp(scratch->window, ref->window, sizeof(ref->window)) == 0);
    static_assert(SMFP_ANALYSIS_FFT_SIZE == REF_FFT_SIZE && SMFP_ANALYSIS_HOP == REF_HOP, "analysis windows changed");
    static_assert(SMFP_DEFAULT_RANGE_START == REF_RANGE_START && SMFP_DEFAULT_RANGE_END == REF_RANGE_END,
                  "default range changed");

    for (int note = 0; note < 128; ++note) CHECK(smfp_root_note_hz(note) == ref_root_note_hz(note));
    CHECK(fabsf(smfp_root_note_hz(69) - 440.0f) < 1e-3f && fabsf(smfp_root_note_hz(57) - 220.0f) < 1e-3f);

    // =============================================================================
    // Zone ranges: FastSampler's own cases, then random ones against the
    // reference.
    // =============================================================================
    {
        SMFPAnalysisRange automatic = { false, SMFP_DEFAULT_RANGE_START, SMFP_DEFAULT_RANGE_END };
        uint32_t rs = 0, re = 0;
        smfp_resolve_zone_range(100000, 0, 100000, true, 20000, 80000, &automatic, &rs, &re);
        CHECK(rs == 20000 && re == 80000);
        smfp_resolve_zone_range(100000, 0, 100000, true, 20000, 80000, nullptr, &rs, &re);
        CHECK(rs == 20000 && re == 80000);
        smfp_resolve_zone_range(100000, 0, 100000, false, 0, 0, &automatic, &rs, &re);
        CHECK(rs == 15000 && re == 85000);
        smfp_resolve_zone_range(100000, 0, 0, false, 0, 0, nullptr, &rs, &re);
        CHECK(rs == 15000 && re == 85000);
        smfp_resolve_zone_range(100000, 0, 100000, true, 20000, 100001, &automatic, &rs, &re);
        CHECK(rs == 15000 && re == 85000);

        SMFPAnalysisRange manual = { true, 0.5f, 0.75f };
        smfp_resolve_zone_range(100000, 0, 100000, true, 20000, 80000, &manual, &rs, &re);
        CHECK(rs == 50000 && re == 75000);
        smfp_resolve_zone_range(100000, 40000, 60000, false, 0, 0, &automatic, &rs, &re);
        CHECK(rs == 43000 && re == 57000);

        SMFPAnalysisRange narrow = { true, 0.0f, 0.01f };
        smfp_resolve_zone_range(100000, 0, 100000, false, 0, 0, &narrow, &rs, &re);
        CHECK(rs == 0 && re == 100000);
        SMFPAnalysisRange reversed = { true, 0.8f, 0.2f };
        smfp_resolve_zone_range(100000, 0, 100000, false, 0, 0, &reversed, &rs, &re);
        CHECK(rs == 15000 && re == 85000);

        uint32_t seed = 7;
        for (int i = 0; i < 100000; ++i) {
            uint32_t total = next_random(&seed) % 200000;
            uint32_t play_start = next_random(&seed) % (total + 2);
            uint32_t play_end = (next_random(&seed) & 3) == 0 ? 0 : next_random(&seed) % (total + 2);
            bool loop = (next_random(&seed) & 1) != 0;
            uint32_t loop_start = next_random(&seed) % (total + 2);
            uint32_t loop_end = next_random(&seed) % (total + 2);
            SMFPAnalysisRange range = { (next_random(&seed) & 1) != 0,
                                        next_noise(&seed) * 0.75f + 0.5f,
                                        next_noise(&seed) * 0.75f + 0.5f };
            uint32_t s0 = 0, e0 = 0, s1 = 1, e1 = 1;
            smfp_resolve_zone_range(total, play_start, play_end, loop, loop_start, loop_end, &range, &s0, &e0);
            ref_resolve_zone_range(total, play_start, play_end, loop, loop_start, loop_end,
                                   range.manual, range.start_frac, range.end_frac, &s1, &e1);
            CHECK(s0 == s1 && e0 == e1);
        }
    }

    // =============================================================================
    // Band analysis against the reference, bit for bit.
    // =============================================================================
    {
        struct Case {
            uint32_t channels, sample_rate, total_frames, range_start, range_end;
            float f0, db_per_octave, noise_level;
        };
        const Case cases[] = {
            { 1, 44100, 44100, 0, 44100, 220.0f, 0.0f, 0.0f },
            { 2, 48000, 48000, 10000, 40000, 110.0f, -6.0f, 0.01f },
            { 1, 96000, 60000, 0, 60000, 0.0f, -3.0f, 0.02f },
            { 2, 44100, 30000, 2000, 50000, 8000.0f, 0.0f, 0.01f },
            { 1, 44100, 20000, 100, 100 + REF_FFT_SIZE + 3 * REF_HOP, 440.0f, -6.0f, 0.0f },
            { 2, 44100, 20000, 0, REF_FFT_SIZE - 1, 440.0f, 0.0f, 0.0f },
            { 3, 22050, 22050, 0, 22050, 55.0f, -9.0f, 0.05f },
        };
        for (const Case& c : cases) {
            std::vector<float> pcm = make_tone(c.total_frames, c.channels, c.sample_rate,
                                               c.f0 > 0.0f ? c.f0 : 330.0f, c.db_per_octave, c.noise_level);
            float bands[SMFP_BAND_COUNT], ref_bands[SMFP_BAND_COUNT];
            uint32_t frames = smfp_analyze_zone_bands(scratch.get(), pcm.data(), c.total_frames, c.channels,
                                                      c.sample_rate, c.range_start, c.range_end, c.f0, bands);
            uint32_t ref_frames = ref_analyse_zone_bands(ref.get(), pcm.data(), c.total_frames, c.channels,
                                                         c.sample_rate, c.range_start, c.range_end, c.f0, ref_bands);
            CHECK(frames == ref_frames);
            CHECK(std::memcmp(bands, ref_bands, sizeof(bands)) == 0);
        }

        std::vector<float> pcm = make_tone(20000, 1, 44100, 440.0f, -6.0f, 0.0f);
        float bands[SMFP_BAND_COUNT];
        CHECK(smfp_analyze_zone_bands(scratch.get(), pcm.data(), 20000, 1, 44100, 100,
                                      100 + SMFP_ANALYSIS_FFT_SIZE + 3 * SMFP_ANALYSIS_HOP, 440.0f, bands) ==
              4 * SMFP_ANALYSIS_HOP);
    }

    // =============================================================================
    // What the measurement is for: FastSampler's own capture checks.
    // =============================================================================
    {
        const uint32_t sr = 44100;
        const uint32_t frames = sr;
        const float f0 = 220.0f;
        // Bands inside the harmonic range; its ends roll off.
        const int first_band = 15;
        const int last_band = 24;

        float bands_flat[SMFP_BAND_COUNT];
        std::vector<float> pcm = make_tone(frames, 1, sr, f0, 0.0f, 0.0f);
        CHECK(smfp_analyze_zone_bands(scratch.get(), pcm.data(), frames, 1, sr, 0, frames, f0, bands_flat) > 0);
        CHECK(band_spread(bands_flat, first_band, last_band) < 1.0f);

        // Without the pitch, the envelope follows the comb of harmonics.
        float bands_raw[SMFP_BAND_COUNT];
        CHECK(smfp_analyze_zone_bands(scratch.get(), pcm.data(), frames, 1, sr, 0, frames, 0.0f, bands_raw) > 0);
        CHECK(band_spread(bands_raw, first_band, last_band) > 6.0f);

        double total = 0.0;
        for (int b = 0; b < SMFP_BAND_COUNT; ++b) total += pow(10.0, (double)bands_flat[b] * 0.1);
        CHECK(fabs(10.0 * log10(total / (double)SMFP_BAND_COUNT)) < 0.01);

        float bands_slope[SMFP_BAND_COUNT];
        pcm = make_tone(frames, 1, sr, f0, -6.0f, 0.0f);
        CHECK(smfp_analyze_zone_bands(scratch.get(), pcm.data(), frames, 1, sr, 0, frames, f0, bands_slope) > 0);
        for (int b = first_band; b + 3 <= last_band; ++b) {
            float per_octave = bands_slope[b + 3] - bands_slope[b];
            CHECK(per_octave < -3.0f && per_octave > -9.0f);
        }

        // The level the zone was played at does not change its fingerprint.
        float bands_quiet[SMFP_BAND_COUNT];
        for (float& v : pcm) v *= 0.25f;
        CHECK(smfp_analyze_zone_bands(scratch.get(), pcm.data(), frames, 1, sr, 0, frames, f0, bands_quiet) > 0);
        for (int b = first_band; b <= last_band; ++b) CHECK(fabsf(bands_quiet[b] - bands_slope[b]) < 0.5f);

        // Nor does measuring the same audio in two channels.
        std::vector<float> stereo((size_t)frames * 2);
        for (uint32_t i = 0; i < frames; ++i) stereo[(size_t)i * 2] = stereo[(size_t)i * 2 + 1] = pcm[i];
        float bands_stereo[SMFP_BAND_COUNT];
        CHECK(smfp_analyze_zone_bands(scratch.get(), stereo.data(), frames, 2, sr, 0, frames, f0, bands_stereo) > 0);
        for (int b = 0; b < SMFP_BAND_COUNT; ++b) CHECK(fabsf(bands_stereo[b] - bands_quiet[b]) < 1e-3f);

        float bands_short[SMFP_BAND_COUNT];
        for (int b = 0; b < SMFP_BAND_COUNT; ++b) bands_short[b] = 99.0f;
        CHECK(smfp_analyze_zone_bands(scratch.get(), pcm.data(), frames, 1, sr, 0, 1000, f0, bands_short) == 0);
        for (int b = 0; b < SMFP_BAND_COUNT; ++b) CHECK(bands_short[b] == 0.0f);
        CHECK(smfp_analyze_zone_bands(scratch.get(), pcm.data(), frames, 1, sr, frames - 10, frames + 10000, f0, bands_short) == 0);
    }

    std::puts("All SMFP analysis checks passed.");
    return 0;
}
