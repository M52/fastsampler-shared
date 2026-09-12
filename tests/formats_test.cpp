// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
#include <pb_encode.h>
#include <pb_decode.h>
#include "fastsampler_formats.h"
#include "fastsampler_fsb.h"
#include "fastsampler_smfp.h"
#include "fastsampler_pb.h"
#include "fastsampler.pb.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main() {
    // An independently specified wire fixture protects field numbers and types.
    unsigned char wire[] = {0x0a, 1, 'x', 0x12, 5, 'x', '.', 'f', 's', 'b'};
    auto message = std::make_unique<FSIFile>();
    std::memset(message.get(), 0, sizeof(FSIFile));
    pb_istream_t input = pb_istream_from_buffer(wire, sizeof(wire));
    CHECK(pb_decode(&input, FSIFile_fields, message.get()));
    CHECK(std::strcmp(message->instrument_name, "x") == 0);
    CHECK(std::strcmp(message->fsb_path, "x.fsb") == 0);
    unsigned char encoded[64] = {};
    pb_ostream_t output = pb_ostream_from_buffer(encoded, sizeof(encoded));
    CHECK(pb_encode(&output, FSIFile_fields, message.get()));
    CHECK(output.bytes_written == sizeof(wire));
    CHECK(std::memcmp(wire, encoded, sizeof(wire)) == 0);
    CHECK(COLLECTION_FX_ROUND_ROBIN == 3 && COLLECTION_FX_LEGATO == 8);
    CHECK(COLLECTION_FX_ROUTER == 18 && COLLECTION_FX_PANNER == 23);
    CHECK(CFX_LEGATO_PARAM_COUNT == 27 && COLLECTION_FLAG_REFERENCED == 8);
    CHECK(sizeof(SMFPHeader) == 80 && sizeof(SMFPZoneEntry) == 408);

    for (unsigned version = 1; version <= 2; ++version) {
        size_t row_size = version == 1 ? sizeof(FSBZoneEntryV1) : sizeof(FSBZoneEntry);
        std::vector<unsigned char> bank(sizeof(FSBHeader) + row_size + 8, 0);
        FSBHeader h = {};
        h.magic = FSB_MAGIC; h.version = version; h.sample_rate = 44100;
        h.channels = 1; h.bps = 16; h.zone_count = 1;
        h.preload_frames = FSB_PRELOAD_ALL_FRAMES;
        h.preload_offset = sizeof(FSBHeader) + row_size; h.preload_size = 8;
        std::memcpy(bank.data(), &h, sizeof(h));
        FSBZoneEntry z = {};
        std::strcpy(z.name, "sample"); z.total_frames = 4;
        z.preload_frame_count = 4; z.sample_frames = 4; z.stream_first_frame = 4;
        if (version == 1) {
            FSBZoneEntryV1 old = {};
            std::strcpy(old.name, "sample"); old.total_frames = 4; old.preload_frame_count = 4;
            std::memcpy(bank.data() + sizeof(h), &old, sizeof(old));
        } else std::memcpy(bank.data() + sizeof(h), &z, sizeof(z));
        FSBHeader actual = {}; FSBZoneEntry row = {};
        CHECK(fsb_load_headers(bank.data(), bank.size(), &actual, &row, 1, 1));
        CHECK(row.total_frames == 4 && row.sample_frames == 4);
        uint64_t base = 99;
        CHECK(fsb_ram_only_sample_bases(&row, 1, 4, &base) && base == 0);
        bank[0] = 0;
        CHECK(!fsb_load_headers(bank.data(), bank.size(), &actual, &row, 1, 1));
    }

    std::vector<float> lo(ZONE_PEAK_CACHE_POINTS * 2), hi(lo.size()), read_lo(lo.size()), read_hi(lo.size());
    for (size_t i = 0; i < lo.size(); ++i) { lo[i] = -(float)i / 4096; hi[i] = (float)i / 4096; }
    CHECK(fsp_pb_write("roundtrip.fsp", lo.data(), hi.data(), 2));
    uint32_t count = 0;
    CHECK(fsp_pb_read("roundtrip.fsp", read_lo.data(), read_hi.data(), 2, &count));
    CHECK(count == 2 && lo == read_lo && hi == read_hi);
    std::remove("roundtrip.fsp");

    SMFPHeader header = {}; SMFPZoneEntry entry = {}, decoded = {};
    header.magic = SMFP_MAGIC_V4; header.version = SMFP_VERSION;
    header.zone_count = 1; header.band_count = SMFP_BAND_COUNT;
    std::strcpy(header.collection_name, "test"); std::strcpy(entry.file_path, "sample.wav");
    entry.sample_rate = 44100; entry.total_frames = 100; entry.root_note = 60;
    for (int b = 0; b < SMFP_BAND_COUNT; ++b) entry.band_db[b] = (float)b - 15;
    CHECK(smfp_save_file("roundtrip.smfp", &header, &entry, 1));
    SMFPHeader decoded_header = {};
    CHECK(smfp_load_file("roundtrip.smfp", &decoded_header, &decoded, 1, &count));
    CHECK(count == 1 && std::memcmp(&entry, &decoded, sizeof(entry)) == 0);
    CHECK(std::fabs(smfp_band_center_hz(17) - 1000.0f) < 0.01f);
    std::remove("roundtrip.smfp");
    std::puts("All four format checks passed.");
    return 0;
}
