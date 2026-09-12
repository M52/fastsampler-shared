// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
#pragma once

#include "fastsampler_format_limits.h"

enum CollectionFXType {
    COLLECTION_FX_NONE = 0,
    COLLECTION_FX_GAIN = 1,
    COLLECTION_FX_PARAMETRIC_EQ = 2,
    COLLECTION_FX_ROUND_ROBIN = 3,
    COLLECTION_FX_TIME_STRETCH = 4,
    COLLECTION_FX_SPECTRAL_MORPH = 5,
    COLLECTION_FX_START_END_OFFSET = 6,
    // =============================================================================
    // Keep serialized type 7 unused. Existing effect numbers must not change.
    // =============================================================================
    COLLECTION_FX_LEGATO = 8,
    COLLECTION_FX_SV_LP1 = 9,
    COLLECTION_FX_SV_LP2 = 10,
    COLLECTION_FX_SV_LP4 = 11,
    COLLECTION_FX_SV_HP1 = 12,
    COLLECTION_FX_SV_HP2 = 13,
    COLLECTION_FX_SV_HP4 = 14,
    COLLECTION_FX_SV_BP2 = 15,
    COLLECTION_FX_SV_BP4 = 16,
    COLLECTION_FX_PARAMETRIC_EQ_5 = 17,
    COLLECTION_FX_ROUTER = 18,
    COLLECTION_FX_PITCH = 19,
    COLLECTION_FX_RANDOM_PITCH = 20,
    COLLECTION_FX_SUSTAIN_PEDAL = 21,
    COLLECTION_FX_VOLUME_ENVELOPE = 22,
    COLLECTION_FX_PANNER = 23,
    COLLECTION_FX_COMPRESSOR = 24
};

enum CFX_GainParams {
    CFX_GAIN_LEVEL_DB = 1,
    CFX_GAIN_LAG_MS = 2
};

enum CFX_ParametricEQParams {
    CFX_EQ_LOW_FREQ   = 1,
    CFX_EQ_LOW_Q      = 2,
    CFX_EQ_LOW_GAIN   = 3,
    CFX_EQ_MID_FREQ   = 4,
    CFX_EQ_MID_Q      = 5,
    CFX_EQ_MID_GAIN   = 6,
    CFX_EQ_HIGH_FREQ  = 7,
    CFX_EQ_HIGH_Q     = 8,
    CFX_EQ_HIGH_GAIN  = 9,
    // =============================================================================
    // Store shape enums as floats for serialization, but exclude them from
    // modulation.
    // =============================================================================
    CFX_EQ_LOW_SHAPE  = 10,
    CFX_EQ_MID_SHAPE  = 11,
    CFX_EQ_HIGH_SHAPE = 12
};

enum CFX_RoundRobinParams {
    CFX_RR_START = 1,
    CFX_RR_END = 2,
    CFX_RR_ACTIVATE_STEP = 3,
    CFX_RR_GLOBAL_COUNTER = 4,
    CFX_RR_MODE = 5
};

enum CFX_SpectralMorphParams {
    CFX_SMF_MORPH     = 1,
    CFX_SMF_SMOOTH_MS = 2,

    // =============================================================================
    // Layer position and boost limit affect fitted tables. Changing them
    // requires a rebuild, so drivers must not target them.
    // =============================================================================
    CFX_SMF_SELF_POS  = 3,
    CFX_SMF_MAX_BOOST = 4
};

enum CFX_TimeStretchParams {
    // =============================================================================
    // Latch section bounds at note-on. Only stretch rate can change during the
    // note.
    // =============================================================================
    CFX_TS_START_TIME = 1,
    CFX_TS_END_TIME   = 2,
    CFX_TS_STRETCH    = 3,
    // =============================================================================
    // Zero grain length selects a value derived from the root note.
    // =============================================================================
    CFX_TS_ANCHOR     = 4,
    CFX_TS_GRAIN_SIZE = 5
};

enum CFX_StartEndOffsetParams {
    CFX_SEO_START_ENABLED = 1,
    CFX_SEO_START_MS      = 2,
    CFX_SEO_END_ENABLED   = 3,
    CFX_SEO_END_MS        = 4
};

enum CFX_SvFilterParams {
    CFX_SV_CUTOFF    = 1,
    CFX_SV_RESONANCE = 2
};

enum CFX_PitchParams {
    CFX_PITCH_CENTS  = 1,
    CFX_PITCH_ENV_ON = 2,
    CFX_PITCH_ENV    = 3
};

enum CFX_RandomPitchParams {
    CFX_RPITCH_MIN_CENTS = 1,
    CFX_RPITCH_MAX_CENTS = 2,
    CFX_RPITCH_ENV_ON    = 3,
    CFX_RPITCH_ENV       = 4
};

enum CFX_VolumeEnvelopeParams {
    CFX_VENV_ENV   = 1,
    CFX_VENV_CURVE = 6
};

enum CFX_PannerParams {
    CFX_PAN_MODE         = 1,
    CFX_PAN_POSITION     = 2,
    CFX_PAN_LAW          = 3,
    CFX_PAN_IMAGE        = 4,
    CFX_PAN_TILT_FREQ    = 5,
    CFX_PAN_TILT_DB      = 6,
    CFX_PAN_WIDTH        = 7,
    CFX_PAN_SHUFFLE_FREQ = 8,
    CFX_PAN_SHUFFLE_DB   = 9
};

enum CFX_CompressorParams {
    CFX_COMP_THRESHOLD_DB = 1,
    CFX_COMP_RATIO        = 2,
    CFX_COMP_KNEE_DB      = 3,
    CFX_COMP_ATTACK_MS    = 4,
    CFX_COMP_RELEASE_MS   = 5,
    CFX_COMP_RELEASE_MODE = 6,
    CFX_COMP_HOLD_MS      = 7,
    CFX_COMP_MAKEUP_DB    = 8,
    CFX_COMP_SIDECHAIN_HZ = 9
};

enum CollectionFXDriverSource {
    FX_DRIVER_SOURCE_CC       = 0,
    FX_DRIVER_SOURCE_VELOCITY = 1,
    FX_DRIVER_SOURCE_LFO      = 2,
    // =============================================================================
    // Latch the transition speed at note-on so it remains constant for that
    // note.
    // =============================================================================
    FX_DRIVER_SOURCE_LEGATO_SPEED = 3,
    FX_DRIVER_SOURCE_NOTE_HOLD = 4,
    // =============================================================================
    // Key position across a note range, which is what spreads a section across
    // the stage. It is latched for the note because the voice's note is.
    // =============================================================================
    FX_DRIVER_SOURCE_NOTE = 5
};

enum LegatoStretchCurve {
    LEGATO_CURVE_LINEAR = 0,
    LEGATO_CURVE_S      = 1,
    LEGATO_CURVE_LATE   = 2,
    LEGATO_CURVE_EARLY  = 3
};

enum LegatoVelocitySource {
    LEGATO_VEL_FROM_NOTE  = 0,
    LEGATO_VEL_FROM_SPEED = 1,

    LEGATO_VEL_FIXED      = 2
};

enum CFX_LegatoParams {
    // =============================================================================
    // The crossfade from the sustain being left into the transition's lead,
    // playing slowly and playing fast. The transition starts this far before
    // its departure. Also the crossfade of a note that has no transition.
    // =============================================================================
    CFX_LEGATO_FADE_IN_SLOW = 1,
    CFX_LEGATO_FADE_IN_FAST = 2,

    // =============================================================================
    // The crossfade from the transition's tail into the sustain landed on,
    // starting at the landing.
    // =============================================================================
    CFX_LEGATO_FADE_OUT     = 3,

    // =============================================================================
    // The gaps since the previous note-on that count as slow and as fast,
    // and the gap under which no transition is played at all.
    // =============================================================================
    CFX_LEGATO_SLOW_AT_MS   = 4,
    CFX_LEGATO_FAST_AT_MS   = 5,
    CFX_LEGATO_FASTEST_MS   = 6,

    // =============================================================================
    // The landing shifted from the arrival marker, and where inside its own
    // sample the sustain landed on starts, past its attack.
    // =============================================================================
    CFX_LEGATO_LAND_OFFSET  = 7,
    CFX_LEGATO_LAND_START   = 8,

    CFX_LEGATO_SHAPE        = 9,
    CFX_LEGATO_WINDOW_MS    = 10,
    CFX_LEGATO_RETURN       = 11,
    CFX_LEGATO_VEL_SOURCE   = 12,
    CFX_LEGATO_VEL_FIXED    = 13,

    // =============================================================================
    // How far the two seams are levelled automatically, in dB either way,
    // and how much of each correction is applied, in percent. Zero on
    // either leaves every take at its own level.
    // =============================================================================
    CFX_LEGATO_MATCH_DB     = 14,
    CFX_LEGATO_MATCH_AMOUNT = 15,

    // =============================================================================
    // How long the transition takes between its departure and its arrival,
    // playing slowly and playing fast, in percent of the recording; and the
    // same for a transition played back the other way at a key release.
    // Only the section between the two markers is stretched.
    // =============================================================================
    CFX_LEGATO_LENGTH_SLOW        = 16,
    CFX_LEGATO_LENGTH_FAST        = 17,
    CFX_LEGATO_RETURN_LENGTH_SLOW = 18,
    CFX_LEGATO_RETURN_LENGTH_FAST = 19,

    // =============================================================================
    // How long that length takes to come on at the departure and to let go
    // at the arrival, and the shape of that ramp. Zero switches the stretch
    // on at the marker.
    // =============================================================================
    CFX_LEGATO_STRETCH_RAMP       = 20,
    CFX_LEGATO_STRETCH_CURVE      = 21,

    // =============================================================================
    // One switch per panel section. Stretch off plays every transition as
    // recorded; the others read their parameters as the defaults, so a
    // section is only in hand once it is switched on. A record written
    // before these existed is loaded with all five on, because every
    // section was in hand then.
    // =============================================================================
    CFX_LEGATO_FADES_ON           = 22,
    CFX_LEGATO_SPEED_ON           = 23,
    CFX_LEGATO_STRETCH_ON         = 24,
    CFX_LEGATO_LANDING_ON         = 25,
    CFX_LEGATO_RULES_ON           = 26
};

enum BusFXType {
    BUS_FX_NONE = 0,
    BUS_FX_GAIN = 1
};

enum BFX_GainParams {
    BFX_GAIN_LEVEL_DB = 0,
    BFX_GAIN_LAG_MS   = 1
};

#define MAX_FX_PER_COLLECTION   8

#define MAX_DRIVERS_PER_FX      8

#define MAX_FX_PARAMS           32

#define COLLECTION_FX_TYPE_COUNT 25

#define CFX_STRENGTH 0

#define CFX_SV_PARAM_COUNT_1POLE 2

#define CFX_SV_PARAM_COUNT       3

#define CFX_PANNER_PARAM_COUNT (CFX_PAN_SHUFFLE_DB + 1)

#define CFX_COMPRESSOR_PARAM_COUNT (CFX_COMP_SIDECHAIN_HZ + 1)

#define CFX_HOLD_RISE_DEFAULT_MS 120.0f

#define CFX_HOLD_FALL_DEFAULT_MS 150.0f

#define LEGATO_DEFAULT_FADE_IN_SLOW_MS 120.0f

#define LEGATO_DEFAULT_FADE_IN_FAST_MS  30.0f

#define LEGATO_DEFAULT_FADE_OUT_MS     150.0f

#define LEGATO_DEFAULT_SLOW_AT_MS      400.0f

#define LEGATO_DEFAULT_FAST_AT_MS       60.0f

#define LEGATO_DEFAULT_START_MS        250.0f

#define LEGATO_DEFAULT_MATCH_DB  6.0f

#define LEGATO_DEFAULT_RAMP_MS  60.0f

#define LEGATO_SHAPE_EQUAL_POWER 2

#define LEGATO_VEL_SOURCE_COUNT 3

#define LEGATO_CURVE_COUNT 4

#define CFX_LEGATO_PARAM_COUNT 27

#define MAX_BFX_PER_BUS 8

#define MAX_BFX_PARAMS 8

#define BUS_FX_TYPE_COUNT 2
