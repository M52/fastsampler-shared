// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
// =============================================================================
// File: fastsampler.pb.c
// Created on: 2026-06-18
// Define manually maintained nanopb bindings for instrument and peak cache
// messages.
// =============================================================================

#include "fastsampler.pb.h"
#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

// =============================================================================
// FSIFile exceeds 64 KB and requires PB_FIELD_32BIT.
// =============================================================================
#ifndef PB_FIELD_32BIT
#error Enable PB_FIELD_32BIT to support messages exceeding 64kB in size: FSIFile
#endif
PB_BIND(FSIFile, FSIFile, 4)

PB_BIND(FSIBus, FSIBus, AUTO)

PB_BIND(FSIBusInsert, FSIBusInsert, AUTO)

PB_BIND(FSIBusSend, FSIBusSend, AUTO)

PB_BIND(FSICollection, FSICollection, AUTO)

PB_BIND(FSIZone, FSIZone, AUTO)

PB_BIND(FSIInstrumentFX, FSIInstrumentFX, 2)

PB_BIND(FSIModulator, FSIModulator, 2)

PB_BIND(FSISMFFingerprints, FSISMFFingerprints, 2)

PB_BIND(FSICollectionFXDriver, FSICollectionFXDriver, 2)

PB_BIND(FSICollectionFX, FSICollectionFX, 4)

PB_BIND(FSICollectionLink, FSICollectionLink, AUTO)

PB_BIND(FSISourceFile, FSISourceFile, 2)

PB_BIND(FSISourceStep, FSISourceStep, AUTO)

PB_BIND(FSISourceSample, FSISourceSample, 2)

PB_BIND(FSPFile, FSPFile, AUTO)

PB_BIND(FSPZone, FSPZone, 4)

