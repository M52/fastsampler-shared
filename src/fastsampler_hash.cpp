// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_hash.cpp
// Created on: 2026-09-13
// Compute XXH64 with seed 0, in one call or over several blocks.
// =============================================================================

#include <cstring>
#include "../include/fastsampler_hash.h"

#define FS_HASH_PRIME1 0x9E3779B185EBCA87ULL
#define FS_HASH_PRIME2 0xC2B2AE3D27D4EB4FULL
#define FS_HASH_PRIME3 0x165667B19E3779F9ULL
#define FS_HASH_PRIME4 0x85EBCA77C2B2AE63ULL
#define FS_HASH_PRIME5 0x27D4EB2F165667C5ULL

static uint64_t fs_hash_rotl(uint64_t v, int r) {
    return (v << r) | (v >> (64 - r));
}

// =============================================================================
// Read little-endian words byte by byte, so the result does not depend on the
// byte order or alignment of the host.
// =============================================================================
static uint64_t fs_hash_read64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

static uint32_t fs_hash_read32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t fs_hash_round(uint64_t acc, uint64_t input) {
    acc += input * FS_HASH_PRIME2;
    acc = fs_hash_rotl(acc, 31);
    return acc * FS_HASH_PRIME1;
}

static uint64_t fs_hash_merge(uint64_t h, uint64_t acc) {
    h ^= fs_hash_round(0, acc);
    return h * FS_HASH_PRIME1 + FS_HASH_PRIME4;
}

static void fs_hash_stripe(FsHash64* h, const uint8_t* p) {
    for (int i = 0; i < 4; ++i) h->acc[i] = fs_hash_round(h->acc[i], fs_hash_read64(p + 8 * i));
}

void fs_hash64_begin(FsHash64* h) {
    memset(h, 0, sizeof(*h));
    h->acc[0] = FS_HASH_PRIME1 + FS_HASH_PRIME2;
    h->acc[1] = FS_HASH_PRIME2;
    h->acc[2] = 0;
    h->acc[3] = 0 - FS_HASH_PRIME1;
}

void fs_hash64_add(FsHash64* h, const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    h->total += size;
    if (h->buffered) {
        size_t take = 32 - h->buffered;
        if (take > size) take = size;
        memcpy(h->buffer + h->buffered, p, take);
        h->buffered += (uint32_t)take;
        p += take;
        size -= take;
        if (h->buffered < 32) return;
        fs_hash_stripe(h, h->buffer);
        h->buffered = 0;
    }
    while (size >= 32) {
        fs_hash_stripe(h, p);
        p += 32;
        size -= 32;
    }
    if (size) {
        memcpy(h->buffer, p, size);
        h->buffered = (uint32_t)size;
    }
}

uint64_t fs_hash64_end(const FsHash64* h) {
    uint64_t r;
    if (h->total >= 32) {
        r = fs_hash_rotl(h->acc[0], 1) + fs_hash_rotl(h->acc[1], 7) +
            fs_hash_rotl(h->acc[2], 12) + fs_hash_rotl(h->acc[3], 18);
        for (int i = 0; i < 4; ++i) r = fs_hash_merge(r, h->acc[i]);
    } else {
        r = FS_HASH_PRIME5;
    }
    r += h->total;

    const uint8_t* p = h->buffer;
    uint32_t n = h->buffered;
    while (n >= 8) {
        r ^= fs_hash_round(0, fs_hash_read64(p));
        r = fs_hash_rotl(r, 27) * FS_HASH_PRIME1 + FS_HASH_PRIME4;
        p += 8;
        n -= 8;
    }
    if (n >= 4) {
        r ^= (uint64_t)fs_hash_read32(p) * FS_HASH_PRIME1;
        r = fs_hash_rotl(r, 23) * FS_HASH_PRIME2 + FS_HASH_PRIME3;
        p += 4;
        n -= 4;
    }
    while (n > 0) {
        r ^= (uint64_t)(*p) * FS_HASH_PRIME5;
        r = fs_hash_rotl(r, 11) * FS_HASH_PRIME1;
        ++p;
        --n;
    }

    r ^= r >> 33;
    r *= FS_HASH_PRIME2;
    r ^= r >> 29;
    r *= FS_HASH_PRIME3;
    r ^= r >> 32;
    return r;
}

uint64_t fs_hash64(const void* data, size_t size) {
    FsHash64 h;
    fs_hash64_begin(&h);
    fs_hash64_add(&h, data, size);
    return fs_hash64_end(&h);
}
