// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
#include <pb_common.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "fastsampler_formats.h"
#include "fastsampler_fsb.h"
#include "fastsampler_smfp.h"
#include "fastsampler_pb.h"
#include "fastsampler.pb.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); return 1; } } while (0)

// Sets every field of a message without repeated fields to its largest
// encoding, so that the encoded size can be compared with the stated size.
static bool fill_largest(const pb_msgdesc_t* fields, void* message) {
    pb_field_iter_t it;
    if (!pb_field_iter_begin(&it, fields, message)) return false;
    do {
        if (PB_HTYPE(it.type) == PB_HTYPE_REPEATED) return false;
        if (PB_HTYPE(it.type) == PB_HTYPE_OPTIONAL) *(bool*)it.pSize = true;
        if (PB_LTYPE(it.type) == PB_LTYPE_STRING) {
            std::memset(it.pData, 'a', it.data_size - 1);
            ((char*)it.pData)[it.data_size - 1] = 0;
        } else if (PB_LTYPE(it.type) == PB_LTYPE_BOOL) {
            *(bool*)it.pData = true;
        } else {
            std::memset(it.pData, 0xFF, it.data_size);
        }
    } while (pb_field_iter_next(&it));
    return true;
}

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
    CHECK(message->banks_count == 0 && !message->has_next_bank_id);

    // Independent wire fixtures for the sample banks: FSIBank tags 1 to 10,
    // FSIFile tags 19 and 20, FSIZone tags 35 and 36, FSICollection tag 12.
    const unsigned char bank_wire[] = {
        0x08, 0x07,
        0x12, 0x05, 'C', 'l', 'o', 's', 'e',
        0x1a, 0x0b, 'C', 'l', 'o', 's', 'e', '/', 'c', '.', 'f', 's', 'b',
        0x20, 0x01,
        0x28, 0x03,
        0x30, 0x80, 0xf7, 0x02,
        0x38, 0x02,
        0x40, 0x18,
        0x49, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x51, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11};
    static_assert(sizeof(bank_wire) == 52, "the FSIBank fixture has 52 bytes");
    FSIBank bank_entry = FSIBank_init_default;
    input = pb_istream_from_buffer(bank_wire, sizeof(bank_wire));
    CHECK(pb_decode(&input, FSIBank_fields, &bank_entry));
    CHECK(bank_entry.has_stable_id && bank_entry.stable_id == 7);
    CHECK(bank_entry.has_name && std::strcmp(bank_entry.name, "Close") == 0);
    CHECK(bank_entry.has_fsb_path && std::strcmp(bank_entry.fsb_path, "Close/c.fsb") == 0);
    CHECK(bank_entry.has_authoring_bus && bank_entry.authoring_bus == 1);
    CHECK(bank_entry.has_row_count && bank_entry.row_count == 3);
    CHECK(bank_entry.has_sample_rate && bank_entry.sample_rate == 48000);
    CHECK(bank_entry.has_channels && bank_entry.channels == 2 && bank_entry.has_bps && bank_entry.bps == 24);
    CHECK(bank_entry.has_table_digest && bank_entry.table_digest == 0x0102030405060708ull);
    CHECK(bank_entry.has_preload_digest && bank_entry.preload_digest == 0x1112131415161718ull);
    unsigned char bank_encoded[64] = {};
    output = pb_ostream_from_buffer(bank_encoded, sizeof(bank_encoded));
    CHECK(pb_encode(&output, FSIBank_fields, &bank_entry));
    CHECK(output.bytes_written == sizeof(bank_wire));
    CHECK(std::memcmp(bank_wire, bank_encoded, sizeof(bank_wire)) == 0);

    std::vector<unsigned char> file_wire = {0x0a, 0x01, 'x', 0x12, 0x05, 'x', '.', 'f', 's', 'i',
                                            0x70, 0x03, 0x9a, 0x01, 0x34};
    file_wire.insert(file_wire.end(), bank_wire, bank_wire + sizeof(bank_wire));
    file_wire.insert(file_wire.end(), {0xa0, 0x01, 0x08});
    CHECK(file_wire.size() == 70);
    std::memset(message.get(), 0, sizeof(FSIFile));
    input = pb_istream_from_buffer(file_wire.data(), file_wire.size());
    CHECK(pb_decode(&input, FSIFile_fields, message.get()));
    CHECK(message->banks_count == 1 && message->has_mixer_revision && message->mixer_revision == 3);
    CHECK(message->has_next_bank_id && message->next_bank_id == 8);
    CHECK(message->banks[0].stable_id == 7 && std::strcmp(message->banks[0].fsb_path, "Close/c.fsb") == 0);
    CHECK(!message->has_bank_sample_rate && !message->has_bank_channels);
    unsigned char file_encoded[128] = {};
    output = pb_ostream_from_buffer(file_encoded, sizeof(file_encoded));
    CHECK(pb_encode(&output, FSIFile_fields, message.get()));
    CHECK(output.bytes_written == file_wire.size());
    CHECK(std::memcmp(file_wire.data(), file_encoded, file_wire.size()) == 0);

    const unsigned char zone_wire[] = {
        0x0a, 0x01, 'z',
        0x15, 0, 0, 0, 0, 0x1d, 0, 0, 0, 0, 0x25, 0, 0, 0, 0, 0x2d, 0, 0, 0, 0,
        0x30, 0x00, 0x38, 0x00, 0x40, 0x00, 0x48, 0x00, 0x50, 0x00, 0x58, 0x00,
        0x65, 0, 0, 0, 0, 0x6d, 0, 0, 0, 0,
        0x98, 0x02, 0x02, 0xa0, 0x02, 0x05};
    static_assert(sizeof(zone_wire) == 51, "the FSIZone fixture has 51 bytes");
    FSIZone zone = FSIZone_init_default;
    input = pb_istream_from_buffer(zone_wire, sizeof(zone_wire));
    CHECK(pb_decode(&input, FSIZone_fields, &zone));
    CHECK(std::strcmp(zone.name, "z") == 0 && !zone.has_source_sample);
    CHECK(zone.has_bank && zone.bank == 2 && zone.has_bank_row && zone.bank_row == 5);
    unsigned char zone_encoded[64] = {};
    output = pb_ostream_from_buffer(zone_encoded, sizeof(zone_encoded));
    CHECK(pb_encode(&output, FSIZone_fields, &zone));
    CHECK(output.bytes_written == sizeof(zone_wire));
    CHECK(std::memcmp(zone_wire, zone_encoded, sizeof(zone_wire)) == 0);

    const unsigned char collection_wire[] = {0x0a, 0x01, 'c', 0x10, 0x00, 0x18, 0x00, 0x20, 0x00, 0x60, 0x09};
    FSICollection collection = FSICollection_init_default;
    input = pb_istream_from_buffer(collection_wire, sizeof(collection_wire));
    CHECK(pb_decode(&input, FSICollection_fields, &collection));
    CHECK(collection.has_mic_copy_of && collection.mic_copy_of == 9 && !collection.has_release_of);
    unsigned char collection_encoded[32] = {};
    output = pb_ostream_from_buffer(collection_encoded, sizeof(collection_encoded));
    CHECK(pb_encode(&output, FSICollection_fields, &collection));
    CHECK(output.bytes_written == sizeof(collection_wire));
    CHECK(std::memcmp(collection_wire, collection_encoded, sizeof(collection_wire)) == 0);

    // An unknown tag (99) is skipped. This is how older readers skip tags 12,
    // 19, 20, 35 and 36.
    const unsigned char unknown_wire[] = {0x0a, 1, 'x', 0x12, 5, 'x', '.', 'f', 's', 'b', 0x98, 0x06, 0x01};
    std::memset(message.get(), 0, sizeof(FSIFile));
    input = pb_istream_from_buffer(unknown_wire, sizeof(unknown_wire));
    CHECK(pb_decode(&input, FSIFile_fields, message.get()));
    CHECK(std::strcmp(message->fsb_path, "x.fsb") == 0 && message->banks_count == 0);

    // A build before 699 maps the file that fsb_path names. In a file with
    // banks that is the .fsi itself, and its FSIP magic fails the bank header
    // check.
    std::vector<unsigned char> framed = {0x46, 0x53, 0x49, 0x50, (unsigned char)file_wire.size(), 0, 0, 0};
    framed.insert(framed.end(), file_wire.begin(), file_wire.end());
    uint32_t framed_magic = 0;
    std::memcpy(&framed_magic, framed.data(), sizeof(framed_magic));
    CHECK(framed_magic == FSI_MAGIC_PB);
    FSBHeader framed_header = {};
    FSBZoneEntry framed_row = {};
    CHECK(!fsb_load_headers(framed.data(), framed.size(), &framed_header, &framed_row, 1, 0));

    static_assert(FSI_MIXER_REVISION == 2 && FSI_MIXER_REVISION_BANKS == 3 && FSI_MIXER_REVISION_NEWEST == 3,
                  "mixer revisions");
    static_assert(FSI_MAX_BANKS == 8 && FSI_MAX_BANKS == sizeof(FSIFile::banks) / sizeof(FSIBank),
                  "the banks capacity is FSI_MAX_BANKS");
    static_assert(FSI_BANK_ID_IMPLICIT == 1 && FSI_BANK_BUS_NONE == 0xFF, "bank constants");
    CHECK(std::strcmp(FASTSAMPLER_FORMATS_RELEASE, "1.2.0") == 0);

    // The stated maximum sizes are the encoded sizes of the largest messages.
    size_t largest = 0;
    FSIBank big_bank = FSIBank_init_zero;
    CHECK(fill_largest(FSIBank_fields, &big_bank));
    CHECK(pb_get_encoded_size(&largest, FSIBank_fields, &big_bank) && largest == (size_t)FSIBank_size);
    FSIZone big_zone = FSIZone_init_zero;
    CHECK(fill_largest(FSIZone_fields, &big_zone));
    CHECK(pb_get_encoded_size(&largest, FSIZone_fields, &big_zone) && largest == (size_t)FSIZone_size);
    FSICollection big_collection = FSICollection_init_zero;
    CHECK(fill_largest(FSICollection_fields, &big_collection));
    CHECK(pb_get_encoded_size(&largest, FSICollection_fields, &big_collection) &&
          largest == (size_t)FSICollection_size);

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
