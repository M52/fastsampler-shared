// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
#pragma once
#include <cstdint>

// Public recipient key for offline FSB provenance encryption. The matching
// private key belongs only in the inspector's external key store, never in
// source control or in a distributed application.
//
// X25519 SubjectPublicKeyInfo (PEM):
// -----BEGIN PUBLIC KEY-----
// MCowBQYDK2VuAyEAhaLOByMavGV1Ddus0dYdvG4sd3yzU9+gMBIaZluhGWg=
// -----END PUBLIC KEY-----
inline constexpr char FSB_PROVENANCE_KEY_ID[] = "fsb-provenance-2026-01";
inline constexpr uint8_t FSB_PROVENANCE_PUBLIC_KEY[32] = {
    0x85, 0xa2, 0xce, 0x07, 0x23, 0x1a, 0xbc, 0x65,
    0x75, 0x0d, 0xdb, 0xac, 0xd1, 0xd6, 0x1d, 0xbc,
    0x6e, 0x2c, 0x77, 0x7c, 0xb3, 0x53, 0xdf, 0xa0,
    0x30, 0x12, 0x1a, 0x66, 0x5b, 0xa1, 0x19, 0x68,
};
static_assert(sizeof(FSB_PROVENANCE_KEY_ID) <= 32, "Provenance key ID is too long");
