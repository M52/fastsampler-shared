// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
#pragma once
#include <cstddef>
#include <cstdint>
#include "fastsampler_provenance_key.h"

#define FSB_PROVENANCE_MAGIC 0x52505346u // FSPR
#define FSB_PROVENANCE_VERSION 1u
#define FSB_PROVENANCE_ALGORITHM 1u // X25519 / BLAKE2b-256 / XChaCha20-Poly1305
#define FSB_PROVENANCE_MAX_RECORDS 16u
#define FSB_PROVENANCE_PLAIN_BYTES 2048u

#pragma pack(push, 1)
// Directly after the 44-byte FSB header, followed by record_count records,
// then the zone table. Version 3 always has at least one record.
struct FSBProvenanceHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t record_count;
    uint32_t record_bytes;
};

struct FSBProvenanceRecord {
    uint32_t version;
    uint32_t algorithm;
    char key_id[32];
    uint8_t ephemeral_public_key[32];
    uint8_t nonce[24];
    uint8_t tag[16];
    uint8_t ciphertext[FSB_PROVENANCE_PLAIN_BYTES];
};

// Serialized as little-endian integers and zero-padded UTF-8 strings. Empty
// optional fields mean unknown. No full paths, hardware IDs or installation ID.
struct FSBProvenanceFields {
    uint32_t version;
    uint64_t created_utc; // Unix seconds, supplied by the exporting computer
    char creator[32];
    char creator_version[64];
    char account_name[256];
    char display_name[256];
    char source_kind[32]; // samples, plugin, or legacy-bank
    char source_format[16]; // e.g. VST2, VST3; empty for imported samples
    char source_id[128];
    char source_name[128];
    char source_vendor[128];
    char source_version[64];
    char preset_name[256];
    char library_name[256];
    char disclaimer_revision[64];
    uint8_t reserved[356]; // must be zero
};
#pragma pack(pop)

struct FSBProvenanceView {
    const FSBProvenanceRecord* records;
    uint32_t count;
};

// Randomness comes from the consumer's OS CSPRNG. Return false on failure;
// the writer must abort, never fall back to plaintext or predictable entropy.
using FSBProvenanceRandom = bool (*)(void* context, uint8_t* bytes, size_t size);

inline FSBProvenanceHeader fsb_provenance_header(uint32_t count) {
    return {FSB_PROVENANCE_MAGIC, FSB_PROVENANCE_VERSION, count, sizeof(FSBProvenanceRecord)};
}
inline uint64_t fsb_provenance_bytes(uint32_t count) {
    return sizeof(FSBProvenanceHeader) + (uint64_t)count * sizeof(FSBProvenanceRecord);
}

// UTF-8, zero-padded; truncates only at a complete code point. Returns false
// for invalid UTF-8. Used by consumers to fill the fixed-capacity fields.
bool fsb_provenance_text(char* out, size_t capacity, const char* text);

// The default recipient is the public key above. The explicit recipient
// overload also supports future keys and independent encryption tests.
bool fsb_provenance_encrypt(const FSBProvenanceFields& fields, FSBProvenanceRandom random,
                             void* context, FSBProvenanceRecord* out);
bool fsb_provenance_encrypt_to(const FSBProvenanceFields& fields, const char* key_id,
                                const uint8_t public_key[32], FSBProvenanceRandom random,
                                void* context, FSBProvenanceRecord* out);
// The inspector supplies the raw X25519 secret extracted from its external
// private key. No private key is built into this library.
bool fsb_provenance_decrypt(const FSBProvenanceRecord& record, const uint8_t private_key[32],
                             FSBProvenanceFields* out);

// Preserve records byte for byte and deduplicate them when combining banks.
// False means capacity exceeded or invalid inputs; never silently drop origins.
bool fsb_provenance_append(FSBProvenanceRecord* records, uint32_t* count,
                            const FSBProvenanceRecord& record);

static_assert(sizeof(FSBProvenanceHeader) == 16, "Provenance header layout changed");
static_assert(sizeof(FSBProvenanceRecord) == 2160, "Encrypted provenance layout changed");
static_assert(sizeof(FSBProvenanceFields) == FSB_PROVENANCE_PLAIN_BYTES, "Provenance fields layout changed");
static_assert(offsetof(FSBProvenanceRecord, tag) == 96, "Provenance associated data changed");
