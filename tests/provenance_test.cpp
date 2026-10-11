// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include "fastsampler_provenance.h"
#include "../3rdparty/monocypher/monocypher.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static void unhex(const char* text, uint8_t* out, size_t size) {
    for (size_t i = 0; i < size; ++i) { unsigned int n = 0; std::sscanf(text + 2 * i, "%2x", &n); out[i] = (uint8_t)n; }
}
static bool fixed_random(void*, uint8_t* out, size_t size) {
    if (size != 56) return false;
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", out, 32);
    for (size_t i = 32; i < size; ++i) out[i] = (uint8_t)(i - 32);
    return true;
}
static bool failed_random(void*, uint8_t*, size_t) { return false; }
static bool cleared(const void* buffer, size_t size) {
    auto* bytes = (const uint8_t*)buffer;
    for (size_t i = 0; i < size; ++i) if (bytes[i]) return false;
    return true;
}

int main() {
    // Independent RFC 7748 key-pair fixture, unrelated to the production key.
    uint8_t secret[32], public_key[32], expected[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", secret, 32);
    unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", expected, 32);
    crypto_x25519_public_key(public_key, secret);
    CHECK(std::memcmp(public_key, expected, 32) == 0);

    FSBProvenanceFields fields{};
    fields.version = FSB_PROVENANCE_VERSION;
    fields.created_utc = 1791676800;
    CHECK(fsb_provenance_text(fields.creator, sizeof(fields.creator), "fastresampler"));
    CHECK(fsb_provenance_text(fields.creator_version, sizeof(fields.creator_version), "build 78"));
    CHECK(fsb_provenance_text(fields.account_name, sizeof(fields.account_name), "Studio"));
    CHECK(fsb_provenance_text(fields.display_name, sizeof(fields.display_name), "Matthias"));
    CHECK(fsb_provenance_text(fields.source_kind, sizeof(fields.source_kind), "plugin"));
    CHECK(fsb_provenance_text(fields.source_format, sizeof(fields.source_format), "VST3"));
    CHECK(fsb_provenance_text(fields.source_name, sizeof(fields.source_name), "Example Instrument"));
    FSBProvenanceRecord record{};
    CHECK(fsb_provenance_encrypt_to(fields, "test-key", public_key, fixed_random, nullptr, &record));
    CHECK(std::memcmp(record.ciphertext, &fields, sizeof(fields)) != 0);
    unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", expected, 32);
    CHECK(std::memcmp(record.ephemeral_public_key, expected, 32) == 0);
    FSBProvenanceFields decoded{};
    CHECK(fsb_provenance_decrypt(record, secret, &decoded));
    CHECK(std::memcmp(&fields, &decoded, sizeof(fields)) == 0);

    // Every encrypted or associated-data byte is authenticated. Output must
    // be cleared on failure, including an incorrect key and low-order peer.
    for (size_t i = 0; i < sizeof(record); ++i) {
        FSBProvenanceRecord corrupt = record;
        ((uint8_t*)&corrupt)[i] ^= 1;
        CHECK(!fsb_provenance_decrypt(corrupt, secret, &decoded));
        CHECK(cleared(&decoded, sizeof(decoded)));
    }
    uint8_t wrong_key[32] = {1};
    CHECK(!fsb_provenance_decrypt(record, wrong_key, &decoded));
    FSBProvenanceRecord low_order = record;
    std::memset(low_order.ephemeral_public_key, 0, 32);
    CHECK(!fsb_provenance_decrypt(low_order, secret, &decoded));
    CHECK(!fsb_provenance_encrypt_to(fields, "test-key", public_key, failed_random, nullptr, &low_order));
    CHECK(cleared(&low_order, sizeof(low_order)));
    uint8_t zero_key[32] = {};
    CHECK(!fsb_provenance_encrypt_to(fields, "test-key", zero_key, fixed_random, nullptr, &low_order));
    fields.reserved[0] = 1;
    CHECK(!fsb_provenance_encrypt_to(fields, "test-key", public_key, fixed_random, nullptr, &low_order));
    fields.reserved[0] = 0;
    CHECK(fsb_provenance_encrypt(fields, fixed_random, nullptr, &low_order));
    CHECK(std::strcmp(low_order.key_id, FSB_PROVENANCE_KEY_ID) == 0);

    char text[4];
    CHECK(fsb_provenance_text(text, sizeof(text), "a\xc3\xa9z") && std::strcmp(text, "a\xc3\xa9") == 0);
    CHECK(fsb_provenance_text(text, 3, "a\xc3\xa9") && std::strcmp(text, "a") == 0);
    CHECK(!fsb_provenance_text(text, sizeof(text), "\xc0\xaf") && text[0] == 0);
    CHECK(!fsb_provenance_text(text, sizeof(text), "\xed\xa0\x80"));
    CHECK(!fsb_provenance_text(text, sizeof(text), "\xf4\x90\x80\x80"));

    FSBProvenanceRecord records[FSB_PROVENANCE_MAX_RECORDS];
    uint32_t count = 0;
    CHECK(fsb_provenance_append(records, &count, record) && count == 1);
    CHECK(fsb_provenance_append(records, &count, record) && count == 1);
    for (uint32_t i = 1; i < FSB_PROVENANCE_MAX_RECORDS; ++i) {
        FSBProvenanceRecord distinct = record; distinct.nonce[0] = (uint8_t)i;
        CHECK(fsb_provenance_append(records, &count, distinct));
    }
    CHECK(count == FSB_PROVENANCE_MAX_RECORDS);
    CHECK(fsb_provenance_append(records, &count, record));
    CHECK(!fsb_provenance_append(records, &count, low_order));
    crypto_wipe(secret, sizeof(secret));
    std::puts("Provenance encryption, validation and preservation checks passed.");
    return 0;
}
