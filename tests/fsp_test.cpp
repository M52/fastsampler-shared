// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// Check the .fsp index, the read of one zone and the streaming write.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <pb_encode.h>
#include "fastsampler_pb.h"
#include "fastsampler.pb.h"

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAILED: %s (line %d)\n", #x, __LINE__); ++failures; } } while (0)

static std::vector<unsigned char> read_file(const char* path) {
    std::vector<unsigned char> bytes;
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return bytes;
    int c;
    while ((c = std::fgetc(fp)) != EOF) bytes.push_back((unsigned char)c);
    std::fclose(fp);
    return bytes;
}

struct Peaks {
    std::vector<float> lo, hi;
    explicit Peaks(uint32_t zones) : lo((size_t)zones * ZONE_PEAK_CACHE_POINTS), hi(lo.size()) {
        for (size_t i = 0; i < lo.size(); ++i) {
            lo[i] = -(float)(i % 977) / 1024.0f;
            hi[i] = (float)(i % 613) / 1024.0f;
        }
    }
};

static bool source_from_peaks(void* user, uint32_t zone, float* lo, float* hi) {
    const Peaks* p = (const Peaks*)user;
    std::memcpy(lo, &p->lo[(size_t)zone * ZONE_PEAK_CACHE_POINTS], ZONE_PEAK_CACHE_POINTS * sizeof(float));
    std::memcpy(hi, &p->hi[(size_t)zone * ZONE_PEAK_CACHE_POINTS], ZONE_PEAK_CACHE_POINTS * sizeof(float));
    return true;
}

static bool source_stops(void* user, uint32_t zone, float* lo, float* hi) {
    return zone < 1 && source_from_peaks(user, zone, lo, hi);
}

static bool stream_write(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    return std::fwrite(buf, 1, count, (FILE*)stream->state) == count;
}

// =============================================================================
// A .fsp that another writer could make: zone z has z + 1 points, and a field
// the reader does not know sits between the first two zones.
// =============================================================================
static bool write_uneven(const char* path, uint32_t zones) {
    FILE* fp = std::fopen(path, "wb");
    if (!fp) return false;
    uint32_t header[2] = { FSP_MAGIC_PB, 0 };
    std::fwrite(header, sizeof(header), 1, fp);
    pb_ostream_t stream = {};
    stream.callback = stream_write;
    stream.state = fp;
    stream.max_size = SIZE_MAX;
    FSPZone* zone = (FSPZone*)std::calloc(1, sizeof(FSPZone));
    bool ok = zone != nullptr;
    for (uint32_t z = 0; ok && z < zones; ++z) {
        std::memset(zone, 0, sizeof(*zone));
        zone->min_peaks_count = zone->max_peaks_count = z + 1;
        for (uint32_t p = 0; p <= z; ++p) {
            zone->min_peaks[p] = -0.25f * (float)(z + 1);
            zone->max_peaks[p] = 0.25f * (float)(z + 1);
        }
        ok = pb_encode_tag(&stream, PB_WT_STRING, 1) && pb_encode_submessage(&stream, FSPZone_fields, zone);
        if (ok && z == 0) ok = pb_encode_tag(&stream, PB_WT_VARINT, 9) && pb_encode_varint(&stream, 300);
    }
    std::free(zone);
    header[1] = (uint32_t)stream.bytes_written;
    std::fseek(fp, 0, SEEK_SET);
    std::fwrite(header, sizeof(header), 1, fp);
    std::fclose(fp);
    return ok;
}

int main() {
    const uint32_t zones = 5;
    Peaks peaks(zones);

    // The streaming write gives the bytes of the write from arrays.
    CHECK(fsp_pb_write("fsp_arrays.fsp", peaks.lo.data(), peaks.hi.data(), zones));
    CHECK(fsp_pb_write_zones("fsp_zones.fsp", zones, source_from_peaks, &peaks));
    std::vector<unsigned char> arrays = read_file("fsp_arrays.fsp");
    CHECK(!arrays.empty() && arrays == read_file("fsp_zones.fsp"));

    // A source that stops fails the write.
    CHECK(!fsp_pb_write_zones("fsp_stopped.fsp", zones, source_stops, &peaks));
    std::remove("fsp_stopped.fsp");

    // The index of an even file keeps only the stride, and each zone reads
    // back as the full read gives it.
    FSPIndex index;
    CHECK(fsp_pb_index("fsp_zones.fsp", &index));
    CHECK(index.zone_count == zones && index.offsets == nullptr && index.lengths == nullptr);
    CHECK(index.stride == index.length + 3 && index.first == 8 + 3);
    CHECK((uint64_t)8 + (uint64_t)zones * index.stride == arrays.size());
    std::vector<float> lo(ZONE_PEAK_CACHE_POINTS), hi(ZONE_PEAK_CACHE_POINTS);
    for (uint32_t z = 0; z < zones; ++z) {
        CHECK(fsp_pb_read_zone("fsp_zones.fsp", &index, z, lo.data(), hi.data()));
        CHECK(std::memcmp(lo.data(), &peaks.lo[(size_t)z * ZONE_PEAK_CACHE_POINTS], lo.size() * sizeof(float)) == 0);
        CHECK(std::memcmp(hi.data(), &peaks.hi[(size_t)z * ZONE_PEAK_CACHE_POINTS], hi.size() * sizeof(float)) == 0);
    }
    CHECK(!fsp_pb_read_zone("fsp_zones.fsp", &index, zones, lo.data(), hi.data()));
    fsp_pb_index_free(&index);

    // An uneven file keeps every record. A zone with fewer points reads with
    // zeros after them.
    CHECK(write_uneven("fsp_uneven.fsp", 4));
    CHECK(fsp_pb_index("fsp_uneven.fsp", &index));
    CHECK(index.zone_count == 4 && index.offsets != nullptr && index.lengths != nullptr);
    for (uint32_t z = 0; z < 4; ++z) {
        CHECK(fsp_pb_read_zone("fsp_uneven.fsp", &index, z, lo.data(), hi.data()));
        CHECK(lo[z] == -0.25f * (float)(z + 1) && hi[z] == 0.25f * (float)(z + 1));
        CHECK(lo[z + 1] == 0.0f && hi[ZONE_PEAK_CACHE_POINTS - 1] == 0.0f);
    }
    fsp_pb_index_free(&index);
    std::vector<float> all_lo(4 * ZONE_PEAK_CACHE_POINTS), all_hi(all_lo.size());
    uint32_t count = 0;
    CHECK(fsp_pb_read("fsp_uneven.fsp", all_lo.data(), all_hi.data(), 4, &count) && count == 4);

    // A file cut inside a record, an empty payload and a wrong magic do not
    // index.
    std::vector<unsigned char> cut(arrays.begin(), arrays.end() - 100);
    FILE* fp = std::fopen("fsp_cut.fsp", "wb");
    std::fwrite(cut.data(), 1, cut.size(), fp);
    std::fclose(fp);
    CHECK(!fsp_pb_index("fsp_cut.fsp", &index) && index.zone_count == 0);
    uint32_t empty[2] = { FSP_MAGIC_PB, 0 };
    fp = std::fopen("fsp_cut.fsp", "wb");
    std::fwrite(empty, sizeof(empty), 1, fp);
    std::fclose(fp);
    CHECK(!fsp_pb_index("fsp_cut.fsp", &index));
    CHECK(!fsp_pb_index("fsp_missing.fsp", &index));
    std::vector<unsigned char> wrong = arrays;
    wrong[0] ^= 1;
    fp = std::fopen("fsp_cut.fsp", "wb");
    std::fwrite(wrong.data(), 1, wrong.size(), fp);
    std::fclose(fp);
    CHECK(!fsp_pb_index("fsp_cut.fsp", &index));

    std::remove("fsp_arrays.fsp");
    std::remove("fsp_zones.fsp");
    std::remove("fsp_uneven.fsp");
    std::remove("fsp_cut.fsp");
    if (failures) return 1;
    std::puts("All FSP index checks passed.");
    return 0;
}
