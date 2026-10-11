// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
#include <cstring>
#include "../include/fastsampler_provenance.h"
#include "../3rdparty/monocypher/monocypher.h"

static size_t provenance_utf8_width(const uint8_t* s, size_t remaining) {
    if (!remaining) return 0;
    uint8_t c = s[0];
    if (c < 0x80) return 1;
    size_t n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!n || n > remaining) return 0;
    for (size_t i = 1; i < n; ++i) if ((s[i] & 0xc0) != 0x80) return 0;
    if (n == 3 && ((c == 0xe0 && s[1] < 0xa0) || (c == 0xed && s[1] >= 0xa0))) return 0;
    if (n == 4 && ((c == 0xf0 && s[1] < 0x90) || (c == 0xf4 && s[1] >= 0x90))) return 0;
    return n;
}

bool fsb_provenance_text(char* out, size_t capacity, const char* text) {
    if (!out || !capacity || !text) return false;
    memset(out, 0, capacity);
    size_t size = strlen(text), at = 0, kept = 0;
    while (at < size) {
        size_t n = provenance_utf8_width((const uint8_t*)text + at, size - at);
        if (!n) { memset(out, 0, capacity); return false; }
        if (at + n < capacity) kept = at + n;
        at += n;
    }
    memcpy(out, text, kept);
    return true;
}

static bool provenance_string_valid(const char* s, size_t capacity) {
    const char* end = (const char*)memchr(s, 0, capacity);
    if (!end) return false;
    size_t size = (size_t)(end - s), at = 0;
    while (at < size) {
        size_t n = provenance_utf8_width((const uint8_t*)s + at, size - at);
        if (!n) return false;
        at += n;
    }
    for (size_t i = size; i < capacity; ++i) if (s[i] != 0) return false;
    return true;
}

static bool provenance_fields_valid(const FSBProvenanceFields& f) {
    if (f.version != FSB_PROVENANCE_VERSION || !f.creator[0] || !f.creator_version[0] || !f.source_kind[0]) return false;
#define PROVENANCE_CHECK_STRING(name) if (!provenance_string_valid(f.name, sizeof(f.name))) return false
    PROVENANCE_CHECK_STRING(creator); PROVENANCE_CHECK_STRING(creator_version);
    PROVENANCE_CHECK_STRING(account_name); PROVENANCE_CHECK_STRING(display_name);
    PROVENANCE_CHECK_STRING(source_kind); PROVENANCE_CHECK_STRING(source_format);
    PROVENANCE_CHECK_STRING(source_id); PROVENANCE_CHECK_STRING(source_name);
    PROVENANCE_CHECK_STRING(source_vendor); PROVENANCE_CHECK_STRING(source_version);
    PROVENANCE_CHECK_STRING(preset_name); PROVENANCE_CHECK_STRING(library_name);
    PROVENANCE_CHECK_STRING(disclaimer_revision);
#undef PROVENANCE_CHECK_STRING
    for (uint8_t b : f.reserved) if (b) return false;
    return true;
}

static void provenance_store_le(uint8_t* out, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) { out[i] = (uint8_t)value; value >>= 8; }
}
static uint64_t provenance_load_le(const uint8_t* in, size_t bytes) {
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= (uint64_t)in[i] << (8 * i);
    return value;
}

// Follow Monocypher's X25519 KDF guidance: never use the raw shared secret
// directly as an encryption key; include both public keys in a stable order.
static bool provenance_session_key(uint8_t key[32], const uint8_t secret[32],
                                    const uint8_t peer[32], const uint8_t ephemeral[32], const uint8_t recipient[32]) {
    uint8_t shared[32], zero[32] = {};
    crypto_x25519(shared, secret, peer);
    if (crypto_verify32(shared, zero) == 0) { crypto_wipe(shared, sizeof(shared)); return false; }
    const char domain[] = "FSB-provenance-v1";
    crypto_blake2b_ctx hash;
    crypto_blake2b_init(&hash, 32);
    crypto_blake2b_update(&hash, (const uint8_t*)domain, sizeof(domain) - 1);
    crypto_blake2b_update(&hash, shared, 32);
    crypto_blake2b_update(&hash, ephemeral, 32);
    crypto_blake2b_update(&hash, recipient, 32);
    crypto_blake2b_final(&hash, key);
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(&hash, sizeof(hash));
    return true;
}

bool fsb_provenance_encrypt_to(const FSBProvenanceFields& fields, const char* key_id,
                                const uint8_t public_key[32], FSBProvenanceRandom random,
                                void* context, FSBProvenanceRecord* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!provenance_fields_valid(fields) || !key_id || !key_id[0] || strlen(key_id) >= sizeof(out->key_id) ||
        !public_key || !random) return false;
    uint8_t entropy[56] = {}, session[32] = {}, plain[FSB_PROVENANCE_PLAIN_BYTES] = {};
    bool ok = random(context, entropy, sizeof(entropy));
    if (ok) {
        out->version = FSB_PROVENANCE_VERSION;
        out->algorithm = FSB_PROVENANCE_ALGORITHM;
        ok = fsb_provenance_text(out->key_id, sizeof(out->key_id), key_id);
        crypto_x25519_public_key(out->ephemeral_public_key, entropy);
        memcpy(out->nonce, entropy + 32, sizeof(out->nonce));
        ok = ok && provenance_session_key(session, entropy, public_key, out->ephemeral_public_key, public_key);
    }
    if (ok) {
        memcpy(plain, &fields, sizeof(fields));
        provenance_store_le(plain, fields.version, 4);
        provenance_store_le(plain + offsetof(FSBProvenanceFields, created_utc), fields.created_utc, 8);
        crypto_aead_lock(out->ciphertext, out->tag, session, out->nonce,
                         (const uint8_t*)out, offsetof(FSBProvenanceRecord, tag), plain, sizeof(plain));
    }
    crypto_wipe(entropy, sizeof(entropy));
    crypto_wipe(session, sizeof(session));
    crypto_wipe(plain, sizeof(plain));
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

bool fsb_provenance_encrypt(const FSBProvenanceFields& fields, FSBProvenanceRandom random,
                             void* context, FSBProvenanceRecord* out) {
    return fsb_provenance_encrypt_to(fields, FSB_PROVENANCE_KEY_ID, FSB_PROVENANCE_PUBLIC_KEY, random, context, out);
}

bool fsb_provenance_decrypt(const FSBProvenanceRecord& record, const uint8_t private_key[32], FSBProvenanceFields* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!private_key || record.version != FSB_PROVENANCE_VERSION || record.algorithm != FSB_PROVENANCE_ALGORITHM ||
        !record.key_id[0] || !provenance_string_valid(record.key_id, sizeof(record.key_id))) return false;
    uint8_t recipient[32], session[32] = {}, plain[FSB_PROVENANCE_PLAIN_BYTES] = {};
    crypto_x25519_public_key(recipient, private_key);
    bool ok = provenance_session_key(session, private_key, record.ephemeral_public_key, record.ephemeral_public_key, recipient);
    if (ok) ok = crypto_aead_unlock(plain, record.tag, session, record.nonce, (const uint8_t*)&record,
                                    offsetof(FSBProvenanceRecord, tag), record.ciphertext, sizeof(plain)) == 0;
    if (ok) {
        memcpy(out, plain, sizeof(*out));
        out->version = (uint32_t)provenance_load_le(plain, 4);
        out->created_utc = provenance_load_le(plain + offsetof(FSBProvenanceFields, created_utc), 8);
        ok = provenance_fields_valid(*out);
    }
    crypto_wipe(session, sizeof(session));
    crypto_wipe(plain, sizeof(plain));
    if (!ok) crypto_wipe(out, sizeof(*out));
    return ok;
}

bool fsb_provenance_append(FSBProvenanceRecord* records, uint32_t* count, const FSBProvenanceRecord& record) {
    if (!records || !count || *count > FSB_PROVENANCE_MAX_RECORDS) return false;
    for (uint32_t i = 0; i < *count; ++i) if (memcmp(&records[i], &record, sizeof(record)) == 0) return true;
    if (*count == FSB_PROVENANCE_MAX_RECORDS) return false;
    records[(*count)++] = record;
    return true;
}
