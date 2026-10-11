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

// A structurally valid encrypted record, without needing a private key.
static std::vector<unsigned char> make_provenance_bank(uint32_t count) {
    uint64_t rows_at = fsb_rows_offset(FSB_VERSION, count);
    std::vector<unsigned char> bank((size_t)(rows_at + sizeof(FSBZoneEntry) + 14), 0);
    FSBHeader h = {};
    h.magic = FSB_MAGIC; h.version = FSB_VERSION; h.sample_rate = 44100;
    h.channels = 1; h.bps = 16; h.zone_count = 1; h.preload_frames = 4;
    h.preload_offset = rows_at + sizeof(FSBZoneEntry); h.preload_size = 8;
    FSBZoneEntry z = {};
    std::strcpy(z.name, "sample");
    z.preload_frame_count = 4; z.total_frames = 8; z.sample_frames = 8; z.stream_first_frame = 4;
    z.stream_offset = h.preload_offset + 8; z.stream_size = 6;
    std::memcpy(bank.data(), &h, sizeof(h));
    FSBProvenanceHeader ph = fsb_provenance_header(count);
    std::memcpy(bank.data() + sizeof(h), &ph, sizeof(ph));
    for (uint32_t i = 0; i < count; ++i) {
        FSBProvenanceRecord r{};
        r.version = FSB_PROVENANCE_VERSION; r.algorithm = FSB_PROVENANCE_ALGORITHM;
        std::strcpy(r.key_id, FSB_PROVENANCE_KEY_ID); r.ephemeral_public_key[0] = 9;
        r.nonce[0] = (uint8_t)i;
        std::memcpy(bank.data() + sizeof(h) + sizeof(ph) + i * sizeof(r), &r, sizeof(r));
    }
    std::memcpy(bank.data() + rows_at, &z, sizeof(z));
    return bank;
}

static int check_provenance_banks() {
    CHECK(fsb_rows_offset(FSB_VERSION_PER_ZONE) == 44 && fsb_rows_offset(FSB_VERSION_SHARED_SAMPLES) == 44);
    CHECK(fsb_rows_offset(FSB_VERSION) == 2220);
    FSBHeader h{}; FSBZoneEntry row{}; FSBProvenanceView provenance{};
    for (uint32_t count : {1u, 2u, FSB_PROVENANCE_MAX_RECORDS}) {
        auto bank = make_provenance_bank(count);
        CHECK(fsb_load_headers(bank.data(), bank.size(), &h, &row, 1, 1));
        CHECK(fsb_read_provenance(bank.data(), bank.size(), &provenance) && provenance.count == count);
        CHECK(std::strcmp(row.name, "sample") == 0 && row.stream_offset == h.preload_offset + 8);
        CHECK(!fsb_load_headers(bank.data(), fsb_rows_offset(FSB_VERSION, count) - 1, &h, &row, 1, 1));
    }
    auto invalid = make_provenance_bank(0);
    CHECK(!fsb_load_headers(invalid.data(), invalid.size(), &h, &row, 1, 1));
    for (size_t offset : {size_t(44), size_t(48), size_t(52), size_t(56), size_t(60), size_t(64)}) {
        auto bank = make_provenance_bank(1);
        bank[offset] ^= 0xff;
        CHECK(!fsb_load_headers(bank.data(), bank.size(), &h, &row, 1, 1));
    }
    auto bank = make_provenance_bank(1);
    std::memset(bank.data() + 68, 'x', 32); // unterminated key ID
    CHECK(!fsb_load_headers(bank.data(), bank.size(), &h, &row, 1, 1));
    bank = make_provenance_bank(1);
    std::memcpy(&h, bank.data(), sizeof(h));
    ++h.preload_offset;
    std::memcpy(bank.data(), &h, sizeof(h));
    CHECK(!fsb_load_headers(bank.data(), bank.size(), &h, &row, 1, 1));
    return 0;
}

int main() {
    static_assert(COLLECTION_FX_SIMULATED_LEGATO == 26 && COLLECTION_FX_CHORUS == 27 &&
                  COLLECTION_FX_DELAY == 28 && BUS_FX_GAIN == 1, "Persisted IDs must not move");
    FSIInstrumentFX effect = FSIInstrumentFX_init_zero;
    effect.has_chorus_enabled = effect.chorus_enabled = true;
    effect.has_delay_enabled = effect.delay_enabled = true;
    effect.chorus_params_count = 3;
    effect.delay_params_count = 5;
    effect.chorus_params[0] = 0.4f;
    effect.delay_params[0] = 0.75f;
    effect.delay_params[4] = 12000.0f;
    unsigned char fx_wire[256]{};
    pb_ostream_t fx_out = pb_ostream_from_buffer(fx_wire, sizeof(fx_wire));
    CHECK(pb_encode(&fx_out, FSIInstrumentFX_fields, &effect));
    FSIInstrumentFX restored = FSIInstrumentFX_init_default;
    pb_istream_t fx_in = pb_istream_from_buffer(fx_wire, fx_out.bytes_written);
    CHECK(pb_decode(&fx_in, FSIInstrumentFX_fields, &restored));
    CHECK(restored.chorus_enabled && restored.delay_enabled);
    CHECK(restored.chorus_params_count == 3 && restored.delay_params_count == 5);
    CHECK(restored.chorus_params[0] == 0.4f && restored.delay_params[4] == 12000.0f);
    fx_in = pb_istream_from_buffer(fx_wire, 0);
    CHECK(pb_decode(&fx_in, FSIInstrumentFX_fields, &restored));
    CHECK(!restored.has_chorus_enabled && !restored.has_delay_enabled);
    CHECK(restored.chorus_params_count == 0 && restored.delay_params_count == 0);

    static_assert(COLLECTION_FX_SV_NOTCH == 29 && COLLECTION_FX_STUTTER == 37 &&
                  COLLECTION_FX_TYPE_COUNT == 38, "Persisted IDs must not move");
    static_assert(BUS_FX_NOTCH == 11 && BUS_FX_COMPRESSOR == 19 && BUS_FX_STUTTER == 20,
                  "Persisted IDs must not move");
    static_assert(DELAY_SYNC == 5 && DELAY_NOTE == 6 && DELAY_PARAM_COUNT == 7, "Parameter slots must not move");
    static_assert(COMPRESSOR_PARAM_COUNT <= MAX_BFX_PARAMS && DELAY_PARAM_COUNT <= MAX_BFX_PARAMS,
                  "Bus inserts store at most MAX_BFX_PARAMS parameters");
    FSIInstrumentFX more = FSIInstrumentFX_init_zero;
    more.has_delay_enabled = more.delay_enabled = true;
    more.delay_params_count = DELAY_PARAM_COUNT;
    more.delay_params[DELAY_SYNC] = 1.0f;
    more.delay_params[DELAY_NOTE] = 13.0f;
    more.has_reverb_enabled = more.reverb_enabled = true;
    more.reverb_params_count = REVERB_PARAM_COUNT;
    more.reverb_params[REVERB_WIDTH] = 0.25f;
    more.has_compressor_enabled = more.compressor_enabled = true;
    more.compressor_params_count = COMPRESSOR_PARAM_COUNT;
    more.compressor_params[COMPRESSOR_AUTO_BYPASS] = 1.0f;
    more.has_stutter_enabled = true;
    more.stutter_params_count = STUTTER_PARAM_COUNT;
    more.stutter_params[STUTTER_MIX] = 0.5f;
    unsigned char more_wire[512]{};
    pb_ostream_t more_out = pb_ostream_from_buffer(more_wire, sizeof(more_wire));
    CHECK(pb_encode(&more_out, FSIInstrumentFX_fields, &more));
    FSIInstrumentFX more_back = FSIInstrumentFX_init_default;
    pb_istream_t more_in = pb_istream_from_buffer(more_wire, more_out.bytes_written);
    CHECK(pb_decode(&more_in, FSIInstrumentFX_fields, &more_back));
    CHECK(more_back.delay_params_count == DELAY_PARAM_COUNT && more_back.delay_params[DELAY_NOTE] == 13.0f);
    CHECK(more_back.reverb_enabled && more_back.reverb_params[REVERB_WIDTH] == 0.25f);
    CHECK(more_back.compressor_params_count == COMPRESSOR_PARAM_COUNT &&
          more_back.compressor_params[COMPRESSOR_AUTO_BYPASS] == 1.0f);
    CHECK(more_back.has_stutter_enabled && !more_back.stutter_enabled && more_back.stutter_params[STUTTER_MIX] == 0.5f);
    CHECK(!more_back.has_notch_enabled && more_back.notch_params_count == 0);

    static_assert(BUS_FX_CONVOLUTION == 21 && BUS_FX_TYPE_COUNT == 22 && CONVOLUTION_PARAM_COUNT == 2,
                  "Persisted IDs must not move");
    static_assert(FSI_MAX_IMPULSE_RESPONSES == sizeof(FSIFile::impulse_responses) / sizeof(FSIFile::impulse_responses[0]),
                  "the impulse response capacity is FSI_MAX_IMPULSE_RESPONSES");
    FSIInstrumentFX conv = FSIInstrumentFX_init_zero;
    conv.has_convolution_enabled = conv.convolution_enabled = true;
    conv.convolution_params_count = CONVOLUTION_PARAM_COUNT;
    conv.convolution_params[CONVOLUTION_IR] = 2.0f;
    unsigned char conv_wire[64]{};
    pb_ostream_t conv_out = pb_ostream_from_buffer(conv_wire, sizeof(conv_wire));
    CHECK(pb_encode(&conv_out, FSIInstrumentFX_fields, &conv));
    FSIInstrumentFX conv_back = FSIInstrumentFX_init_default;
    pb_istream_t conv_in = pb_istream_from_buffer(conv_wire, conv_out.bytes_written);
    CHECK(pb_decode(&conv_in, FSIInstrumentFX_fields, &conv_back));
    CHECK(conv_back.convolution_enabled && conv_back.convolution_params[CONVOLUTION_IR] == 2.0f);

    auto with_irs = std::make_unique<FSIFile>();
    std::memset(with_irs.get(), 0, sizeof(FSIFile));
    std::strcpy(with_irs->instrument_name, "x");
    std::strcpy(with_irs->fsb_path, "x.fsb");
    with_irs->impulse_responses_count = 2;
    std::strcpy(with_irs->impulse_responses[0], "Hall.wav");
    std::strcpy(with_irs->impulse_responses[1], "irs/Room 2.aif");
    std::vector<unsigned char> irs_wire(4096);
    pb_ostream_t irs_out = pb_ostream_from_buffer(irs_wire.data(), irs_wire.size());
    CHECK(pb_encode(&irs_out, FSIFile_fields, with_irs.get()));
    auto irs_back = std::make_unique<FSIFile>();
    std::memset(irs_back.get(), 0, sizeof(FSIFile));
    pb_istream_t irs_in = pb_istream_from_buffer(irs_wire.data(), irs_out.bytes_written);
    CHECK(pb_decode(&irs_in, FSIFile_fields, irs_back.get()));
    CHECK(irs_back->impulse_responses_count == 2 && std::strcmp(irs_back->impulse_responses[1], "irs/Room 2.aif") == 0);
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
    CHECK(!bank_entry.has_purged && !bank_entry.purged);
    unsigned char bank_encoded[64] = {};
    output = pb_ostream_from_buffer(bank_encoded, sizeof(bank_encoded));
    CHECK(pb_encode(&output, FSIBank_fields, &bank_entry));
    CHECK(output.bytes_written == sizeof(bank_wire));
    CHECK(std::memcmp(bank_wire, bank_encoded, sizeof(bank_wire)) == 0);

    const unsigned char purged_wire[] = {0x08, 0x07, 0x58, 0x01};
    FSIBank purged_entry = FSIBank_init_default;
    input = pb_istream_from_buffer(purged_wire, sizeof(purged_wire));
    CHECK(pb_decode(&input, FSIBank_fields, &purged_entry));
    CHECK(purged_entry.stable_id == 7 && purged_entry.has_purged && purged_entry.purged);
    unsigned char purged_encoded[8] = {};
    output = pb_ostream_from_buffer(purged_encoded, sizeof(purged_encoded));
    CHECK(pb_encode(&output, FSIBank_fields, &purged_entry));
    CHECK(output.bytes_written == sizeof(purged_wire));
    CHECK(std::memcmp(purged_wire, purged_encoded, sizeof(purged_wire)) == 0);

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
    CHECK(std::strcmp(FASTSAMPLER_FORMATS_RELEASE, "1.4.0") == 0);

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
        FSBProvenanceView provenance{};
        CHECK(fsb_read_provenance(bank.data(), bank.size(), &provenance));
        CHECK(provenance.count == 0 && provenance.records == nullptr);
        uint64_t base = 99;
        CHECK(fsb_ram_only_sample_bases(&row, 1, 4, &base) && base == 0);
        bank[0] = 0;
        CHECK(!fsb_load_headers(bank.data(), bank.size(), &actual, &row, 1, 1));
    }
    CHECK(check_provenance_banks() == 0);

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
