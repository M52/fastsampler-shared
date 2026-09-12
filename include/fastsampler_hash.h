// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler_hash.h
// Created on: 2026-09-13
// Declare the 64-bit hash of source fingerprints and source file hashes.
// =============================================================================

#pragma once
#include <cstddef>
#include <cstdint>

// =============================================================================
// XXH64 with seed 0. The fingerprints and file hashes in FSI source records
// use it, so its output must never change.
// =============================================================================
struct FsHash64 {
    uint64_t acc[4];
    uint64_t total;
    uint8_t  buffer[32];
    uint32_t buffered;
};

void     fs_hash64_begin(FsHash64* h);
void     fs_hash64_add(FsHash64* h, const void* data, size_t size);
uint64_t fs_hash64_end(const FsHash64* h);
uint64_t fs_hash64(const void* data, size_t size);
