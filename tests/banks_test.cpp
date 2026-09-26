// SPDX-License-Identifier: MIT
// Checks the sample bank rules of an FSI, the checks of bank files against
// their FSI, and the bank digests. With --write-fixture <dir> it also writes
// three_banks.fsi and its three banks, for a manual check with older programs.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <pb_encode.h>
#include <pb_decode.h>
#include "fastsampler_banks.h"
#include "fastsampler_hash.h"
#include "fastsampler_pb.h"
#include "fastsampler.pb.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static const uint32_t N = FS_BANKS_NONE;

static const char* const k_bank_names[3] = {"Close", "Tree", "Room"};
static const char* const k_bank_paths[3] = {"three_banks.close.fsb", "three_banks.tree.fsb", "three_banks.room.fsb"};
static const uint32_t k_zone_bank[6] = {0, 0, 1, 2, 2, 2};
static const uint32_t k_zone_row[6] = {1, 0, 0, 2, 0, 1};
static const uint64_t k_zone_frames[6] = {3, 4, 4, 4, 4, 4};

// A RAM-only 16-bit version 2 bank. Stored sample r has 4 frames at preload
// frame 4r. Every row is named name. With share, row 1 is the window [1, 4)
// of stored sample 0.
static std::vector<uint8_t> make_bank(uint32_t rows, uint32_t channels, uint32_t rate, uint8_t audio_byte,
                                      const char* name, bool share) {
    uint64_t preload_bytes = (uint64_t)rows * 4 * channels * 2;
    uint64_t table = sizeof(FSBHeader) + sizeof(FSBZoneEntry) * rows;
    std::vector<uint8_t> bank((size_t)(table + preload_bytes), 0);
    FSBHeader h = {};
    h.magic = FSB_MAGIC; h.version = FSB_VERSION; h.sample_rate = rate;
    h.channels = channels; h.bps = 16; h.zone_count = rows;
    h.preload_frames = FSB_PRELOAD_ALL_FRAMES;
    h.preload_offset = table; h.preload_size = preload_bytes;
    std::memcpy(bank.data(), &h, sizeof(h));
    for (uint32_t r = 0; r < rows; ++r) {
        FSBZoneEntry z = {};
        std::memcpy(z.name, name, std::strlen(name));
        z.sample_index = r; z.sample_frames = 4; z.stream_first_frame = 4;
        z.preload_frame_offset = 4ull * r; z.preload_frame_count = 4; z.total_frames = 4;
        if (share && r == 1) {
            z.sample_index = 0; z.sample_frame_offset = 1; z.total_frames = 3;
            z.preload_frame_offset = 1; z.preload_frame_count = 3;
        }
        std::memcpy(bank.data() + sizeof(h) + sizeof(z) * r, &z, sizeof(z));
    }
    for (uint64_t i = 0; i < preload_bytes; ++i) bank[(size_t)(table + i)] = (uint8_t)(audio_byte + i);
    return bank;
}

// A version 1 bank: one mono 16-bit sample of 4 frames per row.
static std::vector<uint8_t> make_v1_bank(uint32_t rows) {
    uint64_t preload_bytes = (uint64_t)rows * 4 * 2;
    uint64_t table = sizeof(FSBHeader) + sizeof(FSBZoneEntryV1) * rows;
    std::vector<uint8_t> bank((size_t)(table + preload_bytes), 0x5A);
    FSBHeader h = {};
    h.magic = FSB_MAGIC; h.version = FSB_VERSION_PER_ZONE; h.sample_rate = 44100;
    h.channels = 1; h.bps = 16; h.zone_count = rows;
    h.preload_frames = FSB_PRELOAD_ALL_FRAMES;
    h.preload_offset = table; h.preload_size = preload_bytes;
    std::memcpy(bank.data(), &h, sizeof(h));
    for (uint32_t r = 0; r < rows; ++r) {
        FSBZoneEntryV1 z = {};
        std::memcpy(z.name, "C3", 2);
        z.preload_frame_offset = 4ull * r; z.preload_frame_count = 4; z.total_frames = 4;
        std::memcpy(bank.data() + sizeof(h) + sizeof(z) * r, &z, sizeof(z));
    }
    return bank;
}

struct BankFixture {
    std::vector<uint8_t> bytes[3];
    FsBankFacts facts[3];
};

static int describe_banks(BankFixture* fx) {
    fx->bytes[0] = make_bank(2, 1, 48000, 0x11, "C3", true);
    fx->bytes[1] = make_bank(1, 1, 48000, 0x22, "C3", false);
    fx->bytes[2] = make_bank(3, 2, 48000, 0x33, "C3", false);
    for (int b = 0; b < 3; ++b) {
        FSBHeader h = {};
        FSBZoneEntry rows[8] = {};
        uint32_t row = 0;
        CHECK(fs_banks_describe(fx->bytes[b].data(), fx->bytes[b].size(), &h, rows, 8, &fx->facts[b], &row) == FS_BANKS_OK);
        CHECK(row == N && fx->facts[b].row_count == h.zone_count);
    }
    return 0;
}

// The valid file: three banks with 2, 1 and 3 rows, rows in another order than
// the zones, every zone named C3, and two stored samples.
static void build_file(FSIFile* msg, const BankFixture* fx) {
    std::memset(msg, 0, sizeof(FSIFile));
    std::strcpy(msg->instrument_name, "three banks");
    std::strcpy(msg->fsb_path, "three_banks.fsi");
    msg->has_mixer_revision = true;
    msg->mixer_revision = FSI_MIXER_REVISION_BANKS;
    msg->banks_count = 3;
    for (uint32_t b = 0; b < 3; ++b) {
        FSIBank& e = msg->banks[b];
        e.has_stable_id = true; e.stable_id = 10 + b;
        e.has_name = true; std::strcpy(e.name, k_bank_names[b]);
        e.has_fsb_path = true; std::strcpy(e.fsb_path, k_bank_paths[b]);
        e.has_authoring_bus = true; e.authoring_bus = b;
        fs_banks_facts_to_entry(&fx->facts[b], &e);
    }
    msg->has_next_bank_id = true;
    msg->next_bank_id = 13;
    msg->zones_count = 6;
    for (uint32_t z = 0; z < 6; ++z) {
        FSIZone& zone = msg->zones[z];
        std::strcpy(zone.name, "C3");
        zone.volume = 1.0f;
        zone.has_bank = true; zone.bank = k_zone_bank[z];
        zone.has_bank_row = true; zone.bank_row = k_zone_row[z];
    }
    msg->zones[0].has_source_sample = true; msg->zones[0].source_sample = 1;
    msg->zones[2].has_source_sample = true; msg->zones[2].source_sample = 2;
    msg->source_samples_count = 2;
    msg->collections_count = 3;
    const uint32_t first[3] = {0, 2, 3}, count[3] = {2, 1, 3};
    for (uint32_t c = 0; c < 3; ++c) {
        FSICollection& col = msg->collections[c];
        std::snprintf(col.name, sizeof(col.name), "%s strings", k_bank_names[c]);
        col.first_zone_index = first[c];
        col.zone_count = count[c];
        col.has_stable_id = true; col.stable_id = c + 1;
    }
}

// A file without banks: the zones name no bank.
static void make_legacy(FSIFile* msg) {
    msg->banks_count = 0;
    msg->has_next_bank_id = false;
    msg->mixer_revision = FSI_MIXER_REVISION;
    std::strcpy(msg->fsb_path, "three_banks.close.fsb");
    for (uint32_t z = 0; z < msg->zones_count; ++z) {
        msg->zones[z].has_bank = false; msg->zones[z].bank = 0;
        msg->zones[z].has_bank_row = false; msg->zones[z].bank_row = 0;
    }
}

static int expect_problem(const FSIFile* msg, uint32_t code, uint32_t bank, uint32_t zone, uint32_t row,
                          uint32_t collection, int line) {
    FsBanksProblem p = {};
    uint32_t got = fs_banks_check_file(msg, &p);
    if (got != code || p.code != code || p.bank != bank || p.zone != zone || p.row != row || p.collection != collection) {
        std::fprintf(stderr, "%s:%d: expected %u (bank %d zone %d row %d collection %d), got %u (bank %d zone %d row %d collection %d): %s\n",
                     __FILE__, line, code, (int)bank, (int)zone, (int)row, (int)collection,
                     got, (int)p.bank, (int)p.zone, (int)p.row, (int)p.collection, fs_banks_problem_text(got));
        return 1;
    }
    return 0;
}
#define EXPECT_PROBLEM(msg, code, bank, zone, row, collection) \
    do { if (expect_problem(msg, code, bank, zone, row, collection, __LINE__)) return 1; } while (0)

static bool encode_framed(const FSIFile* msg, std::vector<uint8_t>* out) {
    size_t size = 0;
    if (!pb_get_encoded_size(&size, FSIFile_fields, msg)) return false;
    out->assign(8 + size, 0);
    uint32_t magic = FSI_MAGIC_PB, payload = (uint32_t)size;
    std::memcpy(out->data(), &magic, 4);
    std::memcpy(out->data() + 4, &payload, 4);
    pb_ostream_t stream = pb_ostream_from_buffer(out->data() + 8, size);
    return pb_encode(&stream, FSIFile_fields, msg) && stream.bytes_written == size;
}

static bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), fp) == bytes.size();
    return std::fclose(fp) == 0 && ok;
}

static int test_file_rules(FSIFile* msg, const BankFixture* fx) {
    build_file(msg, fx);
    EXPECT_PROBLEM(msg, FS_BANKS_OK, N, N, N, N);
    CHECK(std::strcmp(fs_banks_problem_text(FS_BANKS_ERR_ROW_TWICE), "two zones name the same bank row") == 0);
    CHECK(std::strcmp(fs_banks_problem_text(999), "the sample banks are not valid") == 0);

    // Every zone reaches its row through the checked bank, including the
    // shared window of 3 frames.
    for (uint32_t z = 0; z < 6; ++z) {
        uint32_t b = msg->zones[z].bank;
        FsBankFacts expect = {};
        fs_banks_facts_from_entry(&msg->banks[b], &expect);
        FSBHeader h = {};
        FSBZoneEntry rows[8] = {};
        uint32_t row = 0;
        CHECK(fs_banks_check_bank(&expect, fx->bytes[b].data(), fx->bytes[b].size(), &h, rows, 8, &row) == FS_BANKS_OK);
        CHECK(rows[msg->zones[z].bank_row].total_frames == k_zone_frames[z]);
        CHECK(std::strcmp(rows[msg->zones[z].bank_row].name, msg->zones[z].name) == 0);
    }

    build_file(msg, fx); msg->banks_count = 9;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_COUNT, N, N, N, N);
    build_file(msg, fx); msg->banks[1].stable_id = 10;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ID_TWICE, 1, N, N, N);
    build_file(msg, fx); msg->banks[2].stable_id = 0;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ID, 2, N, N, N);
    build_file(msg, fx); msg->banks[1].fsb_path[0] = 0;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_PATH, 1, N, N, N);
    build_file(msg, fx); msg->banks[0].authoring_bus = 8;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_BUS, 0, N, N, N);
    build_file(msg, fx); msg->banks[1].authoring_bus = 0;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_BUS, 1, N, N, N);
    build_file(msg, fx); msg->banks[1].has_authoring_bus = false; msg->banks[2].has_authoring_bus = false;
    EXPECT_PROBLEM(msg, FS_BANKS_OK, N, N, N, N);
    build_file(msg, fx); msg->banks[1].sample_rate = 44100;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_RATE, 1, N, N, N);
    build_file(msg, fx); msg->banks[2].channels = 3;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_FORMAT, 2, N, N, N);
    build_file(msg, fx); msg->banks[0].has_preload_digest = false;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_DIGEST, 0, N, N, N);
    build_file(msg, fx); msg->banks[1].row_count = 0;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ROW_COUNT, 1, N, N, N);
    build_file(msg, fx); msg->next_bank_id = 12;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_NEXT_ID, N, N, N, N);
    build_file(msg, fx); msg->has_next_bank_id = false;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_NEXT_ID, N, N, N, N);
    build_file(msg, fx); std::strcpy(msg->fsb_path, "Close/three_banks.fsi");
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_SELF_PATH, N, N, N, N);
    build_file(msg, fx); std::strcpy(msg->fsb_path, "Close\\three_banks.fsi");
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_SELF_PATH, N, N, N, N);
    build_file(msg, fx); msg->zones[3].has_bank_row = false;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ZONE_UNBOUND, N, 3, N, N);
    build_file(msg, fx); msg->zones[0].bank = 3;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ZONE_BANK, N, 0, N, N);
    build_file(msg, fx); msg->zones[4].bank_row = 3;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ZONE_ROW, 2, 4, 3, N);
    build_file(msg, fx); msg->zones[5].bank_row = 2;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ROW_TWICE, 2, 5, 2, N);
    build_file(msg, fx); msg->banks[1].row_count = 2;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ROW_UNUSED, 1, N, 1, N);
    build_file(msg, fx); msg->mixer_revision = FSI_MIXER_REVISION;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_REVISION, N, N, N, N);
    build_file(msg, fx); msg->has_mixer_revision = false;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_REVISION, N, N, N, N);
    build_file(msg, fx); msg->has_bank_sample_rate = true; msg->bank_sample_rate = 48000;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_SOURCE_FORMAT, N, N, N, N);
    build_file(msg, fx);
    msg->zones[1].bank = 1; msg->zones[1].bank_row = 0;
    msg->zones[2].bank = 0; msg->zones[2].bank_row = 0;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_COLLECTION, 1, 1, N, 0);

    // A reader gives a zone in no collection range to collection 0, which
    // would then hold zones of two banks.
    build_file(msg, fx); msg->collections[2].zone_count = 2;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_RANGE, N, 5, N, N);
    build_file(msg, fx); msg->collections_count = 0;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_RANGE, N, 0, N, N);
    build_file(msg, fx); msg->collections[2].zone_count = 4;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_RANGE, N, N, N, 2);
    build_file(msg, fx); msg->collections[1].first_zone_index = 6;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_RANGE, N, N, N, 1);
    build_file(msg, fx); msg->collections[2].zone_count = 0xFFFFFFFFu;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_RANGE, N, N, N, 2);

    // A collection without zones has no range, as the writer makes it after
    // the last zone.
    build_file(msg, fx);
    msg->collections_count = 4;
    msg->collections[3].first_zone_index = 6;
    msg->collections[3].zone_count = 0;
    EXPECT_PROBLEM(msg, FS_BANKS_OK, N, N, N, N);
    msg->collections[3].first_zone_index = 1000;
    EXPECT_PROBLEM(msg, FS_BANKS_OK, N, N, N, N);

    build_file(msg, fx); msg->zones[2].source_sample = 1;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_SOURCE_BANK, 1, 2, N, N);

    // One bank with its rows in another order than the zones is a valid
    // manifest.
    build_file(msg, fx);
    msg->banks_count = 1;
    msg->banks[0].stable_id = FSI_BANK_ID_IMPLICIT;
    msg->next_bank_id = 2;
    msg->zones_count = 2;
    msg->collections_count = 1;
    msg->source_samples_count = 1;
    EXPECT_PROBLEM(msg, FS_BANKS_OK, N, N, N, N);

    // Files without banks.
    build_file(msg, fx); msg->banks_count = 0;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ZONE_BANK, N, 0, N, N);
    build_file(msg, fx); make_legacy(msg);
    EXPECT_PROBLEM(msg, FS_BANKS_OK, N, N, N, N);
    msg->zones[4].has_bank_row = true;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_ZONE_BANK, N, 4, N, N);
    build_file(msg, fx); make_legacy(msg); msg->has_next_bank_id = true; msg->next_bank_id = 1;
    EXPECT_PROBLEM(msg, FS_BANKS_ERR_NEXT_ID, N, N, N, N);
    msg->next_bank_id = 5;
    EXPECT_PROBLEM(msg, FS_BANKS_OK, N, N, N, N);
    return 0;
}

static int test_bank_files(const BankFixture* fx) {
    FSBHeader h = {};
    FSBZoneEntry rows[8] = {};
    uint32_t row = 0;
    FsBankFacts facts = {};

    for (int b = 0; b < 3; ++b)
        CHECK(fs_banks_check_bank(&fx->facts[b], fx->bytes[b].data(), fx->bytes[b].size(), &h, rows, 8, &row) == FS_BANKS_OK);
    CHECK(fs_banks_check_bank(&fx->facts[0], fx->bytes[2].data(), fx->bytes[2].size(), &h, rows, 8, &row) == FS_BANKS_ERR_FILE_ROWS);
    CHECK(fs_banks_check_bank(&fx->facts[2], fx->bytes[2].data(), fx->bytes[2].size(), &h, rows, 2, &row) == FS_BANKS_ERR_FILE_ROWS);

    std::vector<uint8_t> renamed = fx->bytes[0];
    renamed[sizeof(FSBHeader) + 1] = '4';
    CHECK(fs_banks_check_bank(&fx->facts[0], renamed.data(), renamed.size(), &h, rows, 8, &row) == FS_BANKS_ERR_FILE_TABLE);

    facts = fx->facts[2];
    facts.channels = 1;
    CHECK(fs_banks_check_bank(&facts, fx->bytes[2].data(), fx->bytes[2].size(), &h, rows, 8, &row) == FS_BANKS_ERR_FILE_FORMAT);

    std::vector<uint8_t> no_magic = fx->bytes[1];
    std::memset(no_magic.data(), 0, 4);
    CHECK(fs_banks_check_bank(&fx->facts[1], no_magic.data(), no_magic.size(), &h, rows, 8, &row) == FS_BANKS_ERR_FILE_HEADER);
    CHECK(fs_banks_describe(no_magic.data(), no_magic.size(), &h, rows, 8, &facts, &row) == FS_BANKS_ERR_FILE_HEADER);
    CHECK(fs_banks_check_bank(&fx->facts[1], fx->bytes[1].data(), 20, &h, rows, 8, &row) == FS_BANKS_ERR_FILE_HEADER);

    // Row checks, through describe and through check_bank.
    std::vector<uint8_t> long_name = fx->bytes[1];
    std::memset(long_name.data() + sizeof(FSBHeader), 'a', MAX_NAME_LENGTH);
    CHECK(fs_banks_describe(long_name.data(), long_name.size(), &h, rows, 8, &facts, &row) == FS_BANKS_ERR_FILE_ROW_NAME);
    CHECK(row == 0);

    std::vector<uint8_t> disagree = fx->bytes[0];
    FSBZoneEntry second = {};
    std::memcpy(&second, disagree.data() + sizeof(FSBHeader) + sizeof(FSBZoneEntry), sizeof(second));
    second.sample_frames = 5;
    std::memcpy(disagree.data() + sizeof(FSBHeader) + sizeof(FSBZoneEntry), &second, sizeof(second));
    CHECK(fs_banks_describe(disagree.data(), disagree.size(), &h, rows, 8, &facts, &row) == FS_BANKS_ERR_FILE_ROW_SHARED);
    CHECK(row == 1);
    CHECK(fs_banks_check_bank(&fx->facts[0], disagree.data(), disagree.size(), &h, rows, 8, &row) == FS_BANKS_ERR_FILE_ROW_SHARED);

    std::vector<uint8_t> no_stream = fx->bytes[1];
    FSBZoneEntry streamed = {};
    std::memcpy(&streamed, no_stream.data() + sizeof(FSBHeader), sizeof(streamed));
    streamed.preload_frame_count = 2;
    streamed.stream_first_frame = 2;
    std::memcpy(no_stream.data() + sizeof(FSBHeader), &streamed, sizeof(streamed));
    CHECK(fs_banks_describe(no_stream.data(), no_stream.size(), &h, rows, 8, &facts, &row) == FS_BANKS_ERR_FILE_ROW_STREAM);
    CHECK(row == 0);

    std::vector<uint8_t> empty = make_bank(1, 1, 48000, 0, "C3", false);
    FSBHeader empty_header = {};
    std::memcpy(&empty_header, empty.data(), sizeof(empty_header));
    empty_header.zone_count = 0;
    std::memcpy(empty.data(), &empty_header, sizeof(empty_header));
    CHECK(fs_banks_describe(empty.data(), empty.size(), &h, rows, 8, &facts, &row) == FS_BANKS_ERR_FILE_ROWS);
    return 0;
}

static int test_digests(const BankFixture* fx) {
    FSBHeader h = {};
    FSBZoneEntry rows[8] = {};
    uint32_t row = 0;

    // The same rows with other audio: the table digests agree and the preload
    // digests do not.
    std::vector<uint8_t> a = make_bank(2, 1, 48000, 0x11, "C3", true);
    std::vector<uint8_t> b = make_bank(2, 1, 48000, 0x22, "C3", true);
    FsBankFacts fa = {}, fb = {};
    CHECK(fs_banks_describe(a.data(), a.size(), &h, rows, 8, &fa, &row) == FS_BANKS_OK);
    CHECK(fs_banks_describe(b.data(), b.size(), &h, rows, 8, &fb, &row) == FS_BANKS_OK);
    CHECK(fa.table_digest == fb.table_digest && fa.preload_digest != fb.preload_digest);
    CHECK(fs_banks_check_bank(&fa, b.data(), b.size(), &h, rows, 8, &row) == FS_BANKS_OK);

    // The table digest covers the header and the version 2 rows.
    const std::vector<uint8_t>& room = fx->bytes[2];
    CHECK(fs_banks_describe(room.data(), room.size(), &h, rows, 8, &fa, &row) == FS_BANKS_OK);
    CHECK(fs_banks_table_bytes(&h) == 44 + 128 * 3);
    CHECK(fa.table_digest == fs_hash64(room.data(), 44 + 128 * 3));

    // The preload digest covers the whole frames of the preload block. A
    // reader that hashes it in parts gets the same digest.
    uint64_t offset = 0, bytes = 0;
    fs_banks_preload_range(&h, &offset, &bytes);
    CHECK(offset == h.preload_offset && bytes == h.preload_size && bytes == 48);
    CHECK(fa.preload_digest == fs_hash64(room.data() + offset, (size_t)bytes));
    CHECK(fa.preload_digest == fs_banks_preload_digest(room.data(), &h));
    FsHash64 parts;
    fs_hash64_begin(&parts);
    fs_hash64_add(&parts, room.data() + offset, 5);
    fs_hash64_add(&parts, room.data() + offset + 5, 20);
    fs_hash64_add(&parts, room.data() + offset + 25, (size_t)bytes - 25);
    CHECK(fs_hash64_end(&parts) == fa.preload_digest);
    FSBHeader partial = h;
    partial.preload_size += 3;
    fs_banks_preload_range(&partial, &offset, &bytes);
    CHECK(bytes == h.preload_size);

    // A version 1 bank has 104-byte rows.
    std::vector<uint8_t> v1 = make_v1_bank(3);
    CHECK(fs_banks_describe(v1.data(), v1.size(), &h, rows, 8, &fa, &row) == FS_BANKS_OK);
    CHECK(h.version == FSB_VERSION_PER_ZONE && fs_banks_table_bytes(&h) == 44 + 104 * 3);
    CHECK(fa.table_digest == fs_hash64(v1.data(), 44 + 104 * 3));
    CHECK(fs_banks_check_bank(&fa, v1.data(), v1.size(), &h, rows, 8, &row) == FS_BANKS_OK);
    return 0;
}

// The valid file survives an encode and a decode unchanged in its bank
// fields, and the decoded file passes the checks.
static int test_round_trip(FSIFile* msg, const BankFixture* fx, std::vector<uint8_t>* framed) {
    build_file(msg, fx);
    CHECK(encode_framed(msg, framed));
    FSIFile* decoded = (FSIFile*)std::calloc(1, sizeof(FSIFile));
    CHECK(decoded != nullptr);
    pb_istream_t input = pb_istream_from_buffer(framed->data() + 8, framed->size() - 8);
    bool ok = pb_decode(&input, FSIFile_fields, decoded);
    bool same = ok && decoded->banks_count == 3 && decoded->zones_count == 6 &&
                std::memcmp(decoded->banks, msg->banks, sizeof(msg->banks)) == 0 &&
                decoded->next_bank_id == 13 && decoded->zones[3].bank == 2 && decoded->zones[3].bank_row == 2;
    uint32_t code = ok ? fs_banks_check_file(decoded, nullptr) : FS_BANKS_ERR_COUNT;
    std::free(decoded);
    CHECK(ok && same && code == FS_BANKS_OK);
    return 0;
}

int main(int argc, char** argv) {
    FSIFile* msg = (FSIFile*)std::calloc(1, sizeof(FSIFile));
    CHECK(msg != nullptr);
    BankFixture fx;
    std::vector<uint8_t> framed;
    int failed = describe_banks(&fx);
    if (!failed) failed = test_file_rules(msg, &fx);
    if (!failed) failed = test_bank_files(&fx);
    if (!failed) failed = test_digests(&fx);
    if (!failed) failed = test_round_trip(msg, &fx, &framed);
    std::free(msg);
    if (failed) return 1;

    if (argc == 3 && std::strcmp(argv[1], "--write-fixture") == 0) {
        std::string dir = argv[2];
        CHECK(write_bytes(dir + "/three_banks.fsi", framed));
        for (int b = 0; b < 3; ++b) CHECK(write_bytes(dir + "/" + k_bank_paths[b], fx.bytes[b]));
        std::printf("Wrote three_banks.fsi and its three banks to %s.\n", dir.c_str());
    }
    std::puts("All bank checks passed.");
    return 0;
}
