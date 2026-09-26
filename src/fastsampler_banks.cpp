// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_banks.cpp
// Created on: 2026-09-26
// Check the sample banks of an FSI and the bank files that it names, and
// compute the digests that bind a bank file to its FSI. No file I/O.
// =============================================================================

#include <cstring>
#include "../include/fastsampler_banks.h"
#include "../include/fastsampler.pb.h"
#include "../include/fastsampler_hash.h"

#define FS_BANKS_ZONE_CAPACITY       ((uint32_t)(sizeof(((FSIFile*)0)->zones) / sizeof(FSIZone)))
#define FS_BANKS_COLLECTION_CAPACITY ((uint32_t)(sizeof(((FSIFile*)0)->collections) / sizeof(FSICollection)))
#define FS_BANKS_SAMPLE_CAPACITY     ((uint32_t)(sizeof(((FSIFile*)0)->source_samples) / sizeof(FSISourceSample)))

static_assert(FS_BANKS_MAX_ROWS == sizeof(((FSIFile*)0)->zones) / sizeof(FSIZone),
              "every bank row belongs to one zone, so the row limit is the zone capacity");
static_assert(FSI_MAX_BANKS == sizeof(((FSIFile*)0)->banks) / sizeof(FSIBank),
              "the banks capacity must be FSI_MAX_BANKS");
static_assert(FSI_MAX_BANKS < FSI_BANK_BUS_NONE, "a bank bus must never be FSI_BANK_BUS_NONE");
static_assert(FSI_MAX_BANKS <= 32, "the bus mask has 32 bits");
static_assert(FSI_MAX_BANKS < 0xFF, "the stored-sample table marks an unused sample with 0xFF");
static_assert(FS_BANKS_MAX_ROWS < 0xFFFF, "the first-row table marks an unused sample with 0xFFFF");
static_assert(FSI_MIXER_REVISION_BANKS > FSI_MIXER_REVISION, "readers of the legacy revision must refuse banks");
static_assert(FSI_MIXER_REVISION_NEWEST >= FSI_MIXER_REVISION_BANKS, "this release reads files with banks");

static uint32_t fs_banks_fail(FsBanksProblem* out, uint32_t code, uint32_t bank, uint32_t zone,
                              uint32_t row, uint32_t collection) {
    if (out) {
        out->code = code;
        out->bank = bank;
        out->zone = zone;
        out->row = row;
        out->collection = collection;
    }
    return code;
}

static uint32_t fs_banks_min(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

// =============================================================================
// A text field that is not empty and that ends inside its array.
// =============================================================================
static bool fs_banks_text_given(const char* text, size_t capacity) {
    return text[0] != 0 && memchr(text, 0, capacity) != nullptr;
}

static bool fs_banks_has_folder(const char* text, size_t capacity) {
    const char* end = (const char*)memchr(text, 0, capacity);
    size_t length = end ? (size_t)(end - text) : capacity;
    return memchr(text, '/', length) != nullptr || memchr(text, '\\', length) != nullptr;
}

uint32_t fs_banks_check_file(const FSIFile* msg, FsBanksProblem* out) {
    fs_banks_fail(out, FS_BANKS_OK, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
    uint32_t zone_count = fs_banks_min(msg->zones_count, FS_BANKS_ZONE_CAPACITY);

    // =============================================================================
    // A file without banks has one implicit bank. Its zones must not name a
    // bank, and a next bank ID must be above the implicit ID.
    // =============================================================================
    if (msg->banks_count == 0) {
        for (uint32_t z = 0; z < zone_count; ++z) {
            if (msg->zones[z].has_bank || msg->zones[z].has_bank_row)
                return fs_banks_fail(out, FS_BANKS_ERR_ZONE_BANK, FS_BANKS_NONE, z, FS_BANKS_NONE, FS_BANKS_NONE);
        }
        if (msg->has_next_bank_id && msg->next_bank_id <= FSI_BANK_ID_IMPLICIT)
            return fs_banks_fail(out, FS_BANKS_ERR_NEXT_ID, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
        return FS_BANKS_OK;
    }

    // =============================================================================
    // Rules for the whole file. The count is checked before any bank is read.
    // =============================================================================
    if (msg->banks_count > FSI_MAX_BANKS)
        return fs_banks_fail(out, FS_BANKS_ERR_COUNT, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
    if (!msg->has_mixer_revision || msg->mixer_revision < FSI_MIXER_REVISION_BANKS)
        return fs_banks_fail(out, FS_BANKS_ERR_REVISION, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
    if (msg->has_bank_sample_rate || msg->has_bank_channels)
        return fs_banks_fail(out, FS_BANKS_ERR_SOURCE_FORMAT, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
    if (!fs_banks_text_given(msg->fsb_path, sizeof(msg->fsb_path)) ||
        fs_banks_has_folder(msg->fsb_path, sizeof(msg->fsb_path)))
        return fs_banks_fail(out, FS_BANKS_ERR_SELF_PATH, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);

    // =============================================================================
    // Rules for each bank. first_row gives the position of the rows of each
    // bank in the claim bits.
    // =============================================================================
    uint32_t bank_count = msg->banks_count;
    uint32_t first_row[FSI_MAX_BANKS] = {};
    uint32_t total_rows = 0;
    uint32_t max_id = 0;
    uint32_t bus_mask = 0;
    for (uint32_t b = 0; b < bank_count; ++b) {
        const FSIBank& e = msg->banks[b];
        if (!e.has_stable_id || e.stable_id == 0)
            return fs_banks_fail(out, FS_BANKS_ERR_ID, b, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
        for (uint32_t p = 0; p < b; ++p) {
            if (msg->banks[p].stable_id == e.stable_id)
                return fs_banks_fail(out, FS_BANKS_ERR_ID_TWICE, b, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
        }
        if (!e.has_fsb_path || !fs_banks_text_given(e.fsb_path, sizeof(e.fsb_path)))
            return fs_banks_fail(out, FS_BANKS_ERR_PATH, b, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
        if (e.has_authoring_bus) {
            if (e.authoring_bus >= FSI_MAX_BANKS || (bus_mask & (1u << e.authoring_bus)))
                return fs_banks_fail(out, FS_BANKS_ERR_BUS, b, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
            bus_mask |= 1u << e.authoring_bus;
        }
        if (!e.has_sample_rate || !e.has_channels || !e.has_bps ||
            e.sample_rate < FSB_MIN_SAMPLE_RATE || e.sample_rate > FSB_MAX_SAMPLE_RATE ||
            (e.channels != 1 && e.channels != 2) || (e.bps != 16 && e.bps != 24))
            return fs_banks_fail(out, FS_BANKS_ERR_FORMAT, b, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
        if (b > 0 && e.sample_rate != msg->banks[0].sample_rate)
            return fs_banks_fail(out, FS_BANKS_ERR_RATE, b, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
        if (!e.has_table_digest || !e.has_preload_digest)
            return fs_banks_fail(out, FS_BANKS_ERR_DIGEST, b, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
        if (!e.has_row_count || e.row_count == 0 || e.row_count > FS_BANKS_MAX_ROWS)
            return fs_banks_fail(out, FS_BANKS_ERR_ROW_COUNT, b, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);
        first_row[b] = total_rows;
        total_rows += e.row_count;
        if (e.stable_id > max_id) max_id = e.stable_id;
    }

    if (!msg->has_next_bank_id || msg->next_bank_id <= max_id)
        return fs_banks_fail(out, FS_BANKS_ERR_NEXT_ID, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE);

    // =============================================================================
    // Each zone claims one bank row, and no row is claimed twice.
    // =============================================================================
    uint8_t claimed[FSI_MAX_BANKS * FS_BANKS_MAX_ROWS / 8];
    memset(claimed, 0, sizeof(claimed));
    for (uint32_t z = 0; z < zone_count; ++z) {
        const FSIZone& zone = msg->zones[z];
        if (!zone.has_bank || !zone.has_bank_row)
            return fs_banks_fail(out, FS_BANKS_ERR_ZONE_UNBOUND, FS_BANKS_NONE, z, FS_BANKS_NONE, FS_BANKS_NONE);
        if (zone.bank >= bank_count)
            return fs_banks_fail(out, FS_BANKS_ERR_ZONE_BANK, FS_BANKS_NONE, z, FS_BANKS_NONE, FS_BANKS_NONE);
        if (zone.bank_row >= msg->banks[zone.bank].row_count)
            return fs_banks_fail(out, FS_BANKS_ERR_ZONE_ROW, zone.bank, z, zone.bank_row, FS_BANKS_NONE);
        uint32_t bit = first_row[zone.bank] + zone.bank_row;
        uint8_t mask = (uint8_t)(1u << (bit & 7u));
        if (claimed[bit >> 3] & mask)
            return fs_banks_fail(out, FS_BANKS_ERR_ROW_TWICE, zone.bank, z, zone.bank_row, FS_BANKS_NONE);
        claimed[bit >> 3] |= mask;
    }

    // =============================================================================
    // Every row of every bank belongs to a zone.
    // =============================================================================
    for (uint32_t b = 0; b < bank_count; ++b) {
        for (uint32_t r = 0; r < msg->banks[b].row_count; ++r) {
            uint32_t bit = first_row[b] + r;
            if (!(claimed[bit >> 3] & (uint8_t)(1u << (bit & 7u))))
                return fs_banks_fail(out, FS_BANKS_ERR_ROW_UNUSED, b, FS_BANKS_NONE, r, FS_BANKS_NONE);
        }
    }

    // =============================================================================
    // All zones of one collection are in the bank of its first zone. The range
    // of each collection with zones fits in the zones, and each zone is in a
    // range. A reader gives a zone in no range to collection 0, so without
    // these two rules a collection could get zones of more than one bank.
    // =============================================================================
    uint8_t in_range[FS_BANKS_MAX_ROWS / 8];
    memset(in_range, 0, sizeof(in_range));
    uint32_t collection_count = fs_banks_min(msg->collections_count, FS_BANKS_COLLECTION_CAPACITY);
    for (uint32_t c = 0; c < collection_count; ++c) {
        const FSICollection& col = msg->collections[c];
        if (col.zone_count == 0) continue;
        if (col.first_zone_index >= zone_count || col.zone_count > zone_count - col.first_zone_index)
            return fs_banks_fail(out, FS_BANKS_ERR_RANGE, FS_BANKS_NONE, FS_BANKS_NONE, FS_BANKS_NONE, c);
        uint32_t bank = msg->zones[col.first_zone_index].bank;
        for (uint32_t z = col.first_zone_index; z < col.first_zone_index + col.zone_count; ++z) {
            if (msg->zones[z].bank != bank)
                return fs_banks_fail(out, FS_BANKS_ERR_COLLECTION, msg->zones[z].bank, z, FS_BANKS_NONE, c);
            in_range[z >> 3] |= (uint8_t)(1u << (z & 7u));
        }
    }
    for (uint32_t z = 0; z < zone_count; ++z) {
        if (!(in_range[z >> 3] & (uint8_t)(1u << (z & 7u))))
            return fs_banks_fail(out, FS_BANKS_ERR_RANGE, FS_BANKS_NONE, z, FS_BANKS_NONE, FS_BANKS_NONE);
    }

    // =============================================================================
    // A stored sample has the format of one bank, so only zones of that bank
    // use it. A reference out of range is left to the reader, which drops it.
    // =============================================================================
    uint32_t sample_count = fs_banks_min(msg->source_samples_count, FS_BANKS_SAMPLE_CAPACITY);
    uint8_t sample_bank[FS_BANKS_MAX_ROWS];
    static_assert(sizeof(sample_bank) >= sizeof(((FSIFile*)0)->source_samples) / sizeof(FSISourceSample),
                  "one entry per stored sample");
    memset(sample_bank, 0xFF, sizeof(sample_bank));
    for (uint32_t z = 0; z < zone_count; ++z) {
        const FSIZone& zone = msg->zones[z];
        if (!zone.has_source_sample || zone.source_sample == 0 || zone.source_sample > sample_count) continue;
        uint8_t& owner = sample_bank[zone.source_sample - 1];
        if (owner == 0xFF) owner = (uint8_t)zone.bank;
        else if (owner != zone.bank)
            return fs_banks_fail(out, FS_BANKS_ERR_SOURCE_BANK, zone.bank, z, FS_BANKS_NONE, FS_BANKS_NONE);
    }

    return FS_BANKS_OK;
}

void fs_banks_facts_from_entry(const FSIBank* entry, FsBankFacts* out) {
    out->row_count      = entry->has_row_count ? entry->row_count : 0;
    out->sample_rate    = entry->has_sample_rate ? entry->sample_rate : 0;
    out->channels       = entry->has_channels ? entry->channels : 0;
    out->bps            = entry->has_bps ? entry->bps : 0;
    out->table_digest   = entry->has_table_digest ? entry->table_digest : 0;
    out->preload_digest = entry->has_preload_digest ? entry->preload_digest : 0;
}

void fs_banks_facts_to_entry(const FsBankFacts* facts, FSIBank* entry) {
    entry->has_row_count = true;
    entry->row_count = facts->row_count;
    entry->has_sample_rate = true;
    entry->sample_rate = facts->sample_rate;
    entry->has_channels = true;
    entry->channels = facts->channels;
    entry->has_bps = true;
    entry->bps = facts->bps;
    entry->has_table_digest = true;
    entry->table_digest = facts->table_digest;
    entry->has_preload_digest = true;
    entry->preload_digest = facts->preload_digest;
}

uint64_t fs_banks_table_bytes(const FSBHeader* header) {
    uint64_t row_size = header->version == FSB_VERSION_PER_ZONE ? sizeof(FSBZoneEntryV1) : sizeof(FSBZoneEntry);
    return sizeof(FSBHeader) + row_size * header->zone_count;
}

uint64_t fs_banks_table_digest(const uint8_t* view, const FSBHeader* header) {
    return fs_hash64(view, (size_t)fs_banks_table_bytes(header));
}

void fs_banks_preload_range(const FSBHeader* header, uint64_t* out_offset, uint64_t* out_bytes) {
    uint64_t frame_bytes = (uint64_t)header->channels * fsb_bytes_per_sample(header->bps);
    *out_offset = header->preload_offset;
    *out_bytes = fsb_preload_total_frames(*header) * frame_bytes;
}

uint64_t fs_banks_preload_digest(const uint8_t* view, const FSBHeader* header) {
    uint64_t offset = 0, bytes = 0;
    fs_banks_preload_range(header, &offset, &bytes);
    return fs_hash64(view + offset, (size_t)bytes);
}

uint32_t fs_banks_check_rows(const FSBHeader* header, const FSBZoneEntry* rows, uint32_t* out_row) {
    uint32_t dummy_row = FS_BANKS_NONE;
    if (!out_row) out_row = &dummy_row;
    *out_row = FS_BANKS_NONE;
    uint32_t count = header->zone_count;
    if (count > FS_BANKS_MAX_ROWS) return FS_BANKS_ERR_FILE_ROWS;

    // =============================================================================
    // first holds the first row of each stored sample. fsb_load_headers makes
    // sure that every sample_index is below the row count.
    // =============================================================================
    uint16_t first[FS_BANKS_MAX_ROWS];
    for (uint32_t i = 0; i < count; ++i) first[i] = 0xFFFF;
    for (uint32_t i = 0; i < count; ++i) {
        const FSBZoneEntry& r = rows[i];
        *out_row = i;
        if (!memchr(r.name, 0, sizeof(r.name))) return FS_BANKS_ERR_FILE_ROW_NAME;
        if (r.preload_frame_count < r.total_frames && r.stream_size == 0) return FS_BANKS_ERR_FILE_ROW_STREAM;
        if (r.sample_index >= count) return FS_BANKS_ERR_FILE_HEADER;
        uint16_t f = first[r.sample_index];
        if (f != 0xFFFF) {
            const FSBZoneEntry& s = rows[f];
            if (r.sample_frames != s.sample_frames || r.stream_offset != s.stream_offset ||
                r.stream_size != s.stream_size || r.stream_first_frame != s.stream_first_frame)
                return FS_BANKS_ERR_FILE_ROW_SHARED;
        } else {
            first[r.sample_index] = (uint16_t)i;
        }
    }
    *out_row = FS_BANKS_NONE;
    return FS_BANKS_OK;
}

uint32_t fs_banks_describe(const uint8_t* view, uint64_t size, FSBHeader* out_header,
                           FSBZoneEntry* out_rows, uint32_t max_rows, FsBankFacts* out_facts,
                           uint32_t* out_row) {
    if (out_row) *out_row = FS_BANKS_NONE;
    if (!fsb_load_headers(view, size, out_header, out_rows, max_rows, 0)) return FS_BANKS_ERR_FILE_HEADER;
    if (out_header->zone_count == 0) return FS_BANKS_ERR_FILE_ROWS;
    uint32_t code = fs_banks_check_rows(out_header, out_rows, out_row);
    if (code != FS_BANKS_OK) return code;
    out_facts->row_count = out_header->zone_count;
    out_facts->sample_rate = out_header->sample_rate;
    out_facts->channels = out_header->channels;
    out_facts->bps = out_header->bps;
    out_facts->table_digest = fs_banks_table_digest(view, out_header);
    out_facts->preload_digest = fs_banks_preload_digest(view, out_header);
    return FS_BANKS_OK;
}

uint32_t fs_banks_check_bank(const FsBankFacts* expect, const uint8_t* view, uint64_t size,
                             FSBHeader* out_header, FSBZoneEntry* out_rows, uint32_t max_rows,
                             uint32_t* out_row) {
    if (out_row) *out_row = FS_BANKS_NONE;
    if (!view || size < sizeof(FSBHeader)) return FS_BANKS_ERR_FILE_HEADER;
    FSBHeader raw;
    memcpy(&raw, view, sizeof(raw));
    if (raw.magic != FSB_MAGIC) return FS_BANKS_ERR_FILE_HEADER;
    uint32_t row_count = expect->row_count;
    if (raw.zone_count != row_count || row_count == 0 || row_count > max_rows) return FS_BANKS_ERR_FILE_ROWS;
    if (!fsb_load_headers(view, size, out_header, out_rows, row_count, row_count)) return FS_BANKS_ERR_FILE_HEADER;
    uint32_t code = fs_banks_check_rows(out_header, out_rows, out_row);
    if (code != FS_BANKS_OK) return code;
    if (out_header->sample_rate != expect->sample_rate || out_header->channels != expect->channels ||
        out_header->bps != expect->bps)
        return FS_BANKS_ERR_FILE_FORMAT;
    if (fs_banks_table_digest(view, out_header) != expect->table_digest) return FS_BANKS_ERR_FILE_TABLE;
    return FS_BANKS_OK;
}

const char* fs_banks_problem_text(uint32_t code) {
    switch (code) {
    case FS_BANKS_OK:                  return "the sample banks have no problem";
    case FS_BANKS_ERR_COUNT:           return "the file has too many sample banks";
    case FS_BANKS_ERR_REVISION:        return "a file with sample banks must have mixer revision 3 or higher";
    case FS_BANKS_ERR_SOURCE_FORMAT:   return "a file with sample banks must not give one format for all source records";
    case FS_BANKS_ERR_ID:              return "a sample bank has no ID";
    case FS_BANKS_ERR_ID_TWICE:        return "two sample banks have the same ID";
    case FS_BANKS_ERR_PATH:            return "a sample bank has no file";
    case FS_BANKS_ERR_BUS:             return "a sample bank names an output bus that is not valid or that another bank names";
    case FS_BANKS_ERR_FORMAT:          return "a sample bank has no valid format";
    case FS_BANKS_ERR_RATE:            return "the sample banks do not have the same sample rate";
    case FS_BANKS_ERR_DIGEST:          return "a sample bank has no digests";
    case FS_BANKS_ERR_ROW_COUNT:       return "a sample bank has no valid row count";
    case FS_BANKS_ERR_ZONE_UNBOUND:    return "a zone names no bank row";
    case FS_BANKS_ERR_ZONE_BANK:       return "a zone names a sample bank that does not exist";
    case FS_BANKS_ERR_ZONE_ROW:        return "a zone names a bank row that does not exist";
    case FS_BANKS_ERR_ROW_TWICE:       return "two zones name the same bank row";
    case FS_BANKS_ERR_ROW_UNUSED:      return "a bank row belongs to no zone";
    case FS_BANKS_ERR_COLLECTION:      return "the zones of one collection are in different sample banks";
    case FS_BANKS_ERR_NEXT_ID:         return "the next bank ID is not above every bank ID";
    case FS_BANKS_ERR_SOURCE_BANK:     return "zones of two banks use the same stored sample";
    case FS_BANKS_ERR_SELF_PATH:       return "a file with sample banks must give its own file name as its bank";
    case FS_BANKS_ERR_RANGE:           return "a collection names zones that do not exist, or a zone is in no collection";
    case FS_BANKS_ERR_FILE_HEADER:     return "the bank file is not a valid sample bank";
    case FS_BANKS_ERR_FILE_ROWS:       return "the bank file does not have the number of rows this instrument was made with";
    case FS_BANKS_ERR_FILE_FORMAT:     return "the bank file does not have the format this instrument was made with";
    case FS_BANKS_ERR_FILE_TABLE:      return "the bank rows are not the rows this instrument was made with";
    case FS_BANKS_ERR_FILE_PRELOAD:    return "the bank audio is not the audio this instrument was made with";
    case FS_BANKS_ERR_FILE_ROW_NAME:   return "a bank row has a name that is too long";
    case FS_BANKS_ERR_FILE_ROW_SHARED: return "bank rows that share a stored sample do not agree";
    case FS_BANKS_ERR_FILE_ROW_STREAM: return "a streamed bank row has no stream";
    default:                           return "the sample banks are not valid";
    }
}
