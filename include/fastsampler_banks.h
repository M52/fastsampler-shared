// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_banks.h
// Created on: 2026-09-26
// Declare the checks of the sample banks of an FSI and of the bank files that
// it names, and the digests that bind a bank file to its FSI.
// =============================================================================

#pragma once
#include <cstdint>
#include "fastsampler_format_limits.h"
#include "fastsampler_fsb.h"

struct _FSIFile;
struct _FSIBank;

// =============================================================================
// The row limit of one bank. It is equal to the zone capacity of FSIFile,
// because every bank row belongs to one zone.
// =============================================================================
#define FS_BANKS_MAX_ROWS 4096
#define FS_BANKS_NONE 0xFFFFFFFFu

#define FS_BANKS_OK 0u

// =============================================================================
// Problems of an FSI, from fs_banks_check_file.
// =============================================================================
#define FS_BANKS_ERR_COUNT           1u  // more than FSI_MAX_BANKS banks
#define FS_BANKS_ERR_REVISION        2u  // banks without mixer_revision 3 or more
#define FS_BANKS_ERR_SOURCE_FORMAT   3u  // banks with bank_sample_rate or bank_channels
#define FS_BANKS_ERR_ID              4u  // a bank without a stable_id, or with 0
#define FS_BANKS_ERR_ID_TWICE        5u  // two banks with the same stable_id
#define FS_BANKS_ERR_PATH            6u  // a bank without an fsb_path
#define FS_BANKS_ERR_BUS             7u  // authoring_bus not below FSI_MAX_BANKS, or given twice
#define FS_BANKS_ERR_FORMAT          8u  // a bank without a valid rate, channel count or bps
#define FS_BANKS_ERR_RATE            9u  // a bank rate that is not the rate of bank 0
#define FS_BANKS_ERR_DIGEST         10u  // a bank without both digests
#define FS_BANKS_ERR_ROW_COUNT      11u  // a row_count that is absent, 0 or above FS_BANKS_MAX_ROWS
#define FS_BANKS_ERR_ZONE_UNBOUND   12u  // a zone without bank or bank_row
#define FS_BANKS_ERR_ZONE_BANK      13u  // a zone bank that does not exist, also in a file without banks
#define FS_BANKS_ERR_ZONE_ROW       14u  // a zone row at or above the row_count of its bank
#define FS_BANKS_ERR_ROW_TWICE      15u  // two zones with the same bank row
#define FS_BANKS_ERR_ROW_UNUSED     16u  // a bank row that no zone names
#define FS_BANKS_ERR_COLLECTION     17u  // the zones of one collection in more than one bank
#define FS_BANKS_ERR_NEXT_ID        18u  // next_bank_id absent with banks, or not above every ID
#define FS_BANKS_ERR_SOURCE_BANK    19u  // one stored sample used by zones of two banks
#define FS_BANKS_ERR_SELF_PATH      20u  // banks, and fsb_path empty or with a folder part
#define FS_BANKS_ERR_RANGE          21u  // a collection range past the last zone, or a zone in no collection range

// =============================================================================
// Problems of a bank file against what its FSI records.
// =============================================================================
#define FS_BANKS_ERR_FILE_HEADER     32u  // not a valid FSB
#define FS_BANKS_ERR_FILE_ROWS       33u  // a different row count, or no rows
#define FS_BANKS_ERR_FILE_FORMAT     34u  // a different rate, channel count or bps
#define FS_BANKS_ERR_FILE_TABLE      35u  // a different table digest
#define FS_BANKS_ERR_FILE_PRELOAD    36u  // a different preload digest; the reader of the preload reports it
#define FS_BANKS_ERR_FILE_ROW_NAME   37u  // a row name without a terminating zero
#define FS_BANKS_ERR_FILE_ROW_SHARED 38u  // rows of one stored sample that do not agree
#define FS_BANKS_ERR_FILE_ROW_STREAM 39u  // a streamed row without stream bytes

// =============================================================================
// The first problem that a check found. An index is FS_BANKS_NONE when it does
// not apply. A bank index that is not FS_BANKS_NONE is a valid index into
// FSIFile.banks.
// =============================================================================
struct FsBanksProblem {
    uint32_t code;
    uint32_t bank;
    uint32_t zone;
    uint32_t row;
    uint32_t collection;
};

// =============================================================================
// What an FSI records about a bank, or what a bank file holds.
// =============================================================================
struct FsBankFacts {
    uint32_t row_count;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bps;
    uint64_t table_digest;
    uint64_t preload_digest;
};

// =============================================================================
// Checks the bank rules of a decoded or built FSIFile. out can be null. Uses
// about 9 KB of stack. Do not call it on the audio thread.
// =============================================================================
uint32_t fs_banks_check_file(const struct _FSIFile* msg, FsBanksProblem* out);

// =============================================================================
// Copies fields 5 to 10 of a bank entry, with 0 for an absent field, and back.
// The second function also sets their has_ flags.
// =============================================================================
void fs_banks_facts_from_entry(const struct _FSIBank* entry, FsBankFacts* out);
void fs_banks_facts_to_entry(const FsBankFacts* facts, struct _FSIBank* entry);

// =============================================================================
// The header and rows as the bank stores them: 44 bytes plus 104 bytes per row
// for version 1, or 128 bytes per row for version 2. The digest functions need
// a header that fsb_load_headers accepted for this view.
// =============================================================================
uint64_t fs_banks_table_bytes(const FSBHeader* header);
uint64_t fs_banks_table_digest(const uint8_t* view, const FSBHeader* header);

// =============================================================================
// The preload bytes that a player reads: the whole frames of the preload block,
// from preload_offset. A player that reads them in parts adds each part to an
// FsHash64 in order and compares the result with the preload digest.
// =============================================================================
void     fs_banks_preload_range(const FSBHeader* header, uint64_t* out_offset, uint64_t* out_bytes);
uint64_t fs_banks_preload_digest(const uint8_t* view, const FSBHeader* header);

// =============================================================================
// Checks rows that fsb_load_headers returned: each name has a terminating zero,
// a streamed row has stream bytes, and rows of one stored sample agree. On a
// problem, out_row gets the row. Uses about 8 KB of stack.
// =============================================================================
uint32_t fs_banks_check_rows(const FSBHeader* header, const FSBZoneEntry* rows, uint32_t* out_row);

// =============================================================================
// Reads, checks and measures a bank file that a writer is about to name. It
// reads every preload byte, so call it on a worker thread. out_rows needs
// max_rows entries. out_row can be null.
// =============================================================================
uint32_t fs_banks_describe(const uint8_t* view, uint64_t size, FSBHeader* out_header,
                           FSBZoneEntry* out_rows, uint32_t max_rows, FsBankFacts* out_facts,
                           uint32_t* out_row);

// =============================================================================
// Checks a bank file against the facts that its FSI records, before a player
// uses it. It does not read the preload: the player compares the preload digest
// while it reads the preload, and reports FS_BANKS_ERR_FILE_PRELOAD. out_rows
// needs max_rows entries. out_row can be null.
// =============================================================================
uint32_t fs_banks_check_bank(const FsBankFacts* expect, const uint8_t* view, uint64_t size,
                             FSBHeader* out_header, FSBZoneEntry* out_rows, uint32_t max_rows,
                             uint32_t* out_row);

// =============================================================================
// A lower-case sentence for a problem code.
// =============================================================================
const char* fs_banks_problem_text(uint32_t code);
