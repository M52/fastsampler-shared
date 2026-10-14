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
    COLLECTION_FX_COMPRESSOR = 24,
    COLLECTION_FX_SMART_RELEASE = 25,
    COLLECTION_FX_SIMULATED_LEGATO = 26,
    COLLECTION_FX_CHORUS = 27,
    COLLECTION_FX_DELAY = 28,
    COLLECTION_FX_SV_NOTCH = 29,
    COLLECTION_FX_REVERB = 30,
    COLLECTION_FX_PHASER = 31,
    COLLECTION_FX_PITCH_SHIFT = 32,
    COLLECTION_FX_WAVE_FOLDER = 33,
    COLLECTION_FX_WAVE_SHAPER = 34,
    COLLECTION_FX_STEREO_SIMULATOR = 35,
    COLLECTION_FX_BIT_CRUSHER = 36,
    COLLECTION_FX_STUTTER = 37,
    COLLECTION_FX_SIPS_LEGATO = 38
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

// =============================================================================
// A release sample is brought to the sustain it ends: to the level the
// sustain was heard at as the key came up, and in over the release that
// sustain fades out over. Each half has a switch. The level half is a
// difference in dB held within a reach, scaled by an amount, with an offset
// on top; the attack half is a share of the sustain's release, with a bend.
// =============================================================================
enum CFX_SmartReleaseParams {
    CFX_SREL_LEVEL_ON      = 1,
    CFX_SREL_LEVEL_DB      = 2,
    CFX_SREL_LEVEL_AMOUNT  = 3,
    CFX_SREL_LEVEL_OFFSET  = 4,
    CFX_SREL_ATTACK_ON     = 5,
    CFX_SREL_ATTACK_PCT    = 6,
    CFX_SREL_ATTACK_BEND   = 7
};

// =============================================================================
// Simulated legato: a connected note is played from the sustain of the note
// it leaves, with no recorded transition. The note left bends toward the
// note reached while it fades, the note reached enters past its attack and
// bends up to its own pitch as it comes in, and both dip a little where they
// cross. The family and the profile name the instrument and so the figures;
// the style is what the player asks for at the key. The enumerated values
// are stored, so they are numbered here. The layout was compacted in
// September 2026 when the effect was rebuilt: a record written by an earlier
// build reads its figures into the wrong slots.
// =============================================================================
enum CFX_SimulatedLegatoParams {
    CFX_SIMLEG_FAMILY          = 1,
    CFX_SIMLEG_PROFILE         = 2,
    CFX_SIMLEG_STYLE           = 3,
    // =============================================================================
    // The crossfade between the two notes, in milliseconds, and the bend:
    // how far the two notes lean toward each other at a semitone, in cents,
    // and how long the lean takes. The portamento is the share of the rest
    // of the interval the bend covers, up to the whole of it, over the glide
    // time an octave takes.
    // =============================================================================
    CFX_SIMLEG_XFADE_MS        = 4,
    CFX_SIMLEG_BEND_CENTS      = 5,
    CFX_SIMLEG_BEND_MS         = 6,
    CFX_SIMLEG_PORTAMENTO      = 7,
    CFX_SIMLEG_GLIDE_MS        = 8,
    CFX_SIMLEG_DIP_DB          = 9,
    // =============================================================================
    // Where a connected note enters its sample past the onset, and how far a
    // draw per key moves that later.
    // =============================================================================
    CFX_SIMLEG_ENTRY_MS        = 10,
    CFX_SIMLEG_ENTRY_SPREAD_MS = 11,
    CFX_SIMLEG_SPEED_RESPONSE  = 12,
    CFX_SIMLEG_GAP_MS          = 13,
    CFX_SIMLEG_RETURN          = 14,
    CFX_SIMLEG_REPEAT          = 15,
    CFX_SIMLEG_SAMPLED_POLICY  = 16,
    CFX_SIMLEG_MAX_INTERVAL    = 17,
    // =============================================================================
    // How much of its key release a note a connection leaves behind keeps,
    // when it has release samples to carry it.
    // =============================================================================
    CFX_SIMLEG_RELEASES        = 18
};

enum SimLegatoFamily {
    SIMLEG_FAMILY_STRINGS   = 0,
    SIMLEG_FAMILY_BRASS     = 1,
    SIMLEG_FAMILY_WOODWINDS = 2
};

enum SimLegatoStringsProfile {
    SIMLEG_STRINGS_VIOLIN  = 0,
    SIMLEG_STRINGS_CELLO   = 1,
    SIMLEG_STRINGS_SECTION = 2,
    SIMLEG_STRINGS_BASS    = 3
};

enum SimLegatoBrassProfile {
    SIMLEG_BRASS_HORN     = 0,
    SIMLEG_BRASS_TRUMPET  = 1,
    SIMLEG_BRASS_TROMBONE = 2,
    SIMLEG_BRASS_LOW      = 3
};

enum SimLegatoWoodwindsProfile {
    SIMLEG_WOODWINDS_FLUTE    = 0,
    SIMLEG_WOODWINDS_CLARINET = 1,
    SIMLEG_WOODWINDS_OBOE     = 2,
    SIMLEG_WOODWINDS_BASSOON  = 3
};

enum SimLegatoStyle {
    SIMLEG_STYLE_SLUR         = 0,
    SIMLEG_STYLE_REARTICULATE = 1,
    SIMLEG_STYLE_PORTAMENTO   = 2
};

enum SimLegatoRepeat {
    SIMLEG_REPEAT_REARTICULATE = 0,
    SIMLEG_REPEAT_RETRIGGER    = 1,
    SIMLEG_REPEAT_HOLD         = 2
};

enum SimLegatoSampledPolicy {
    SIMLEG_SAMPLED_ALWAYS_SIMULATE = 0,
    SIMLEG_SAMPLED_FILL_MISSING    = 1
};

// =============================================================================
// SIPS legato: monophonic legato with portamento. A key
// played over the held one plays its sustain from an offset into the sample;
// the two notes crossfade over XTime and bend toward each other over BTime by
// MinBend cents at a semitone, Octave times that at an octave. Portamento
// glides through chromatic notes, two at a time. Times are milliseconds, fades
// and PBend are percent, keys are MIDI notes. The enumerated values are stored.
// =============================================================================
enum CFX_SipsLegatoParams {
    CFX_SIPS_MODE           = 1,
    CFX_SIPS_XTIME_MS       = 2,
    CFX_SIPS_ATK_FADE       = 3,
    CFX_SIPS_NODE_VOL       = 4,
    CFX_SIPS_RLS_FADE       = 5,
    CFX_SIPS_RLS_MODE       = 6,
    CFX_SIPS_BTIME_MS       = 7,
    CFX_SIPS_MIN_BEND       = 8,
    CFX_SIPS_OCTAVE         = 9,
    CFX_SIPS_OFFSET_MODE    = 10,
    CFX_SIPS_LEG_OFFSET_MS  = 11,
    // =============================================================================
    // PTime is the script's own figure: each glide step waits 0.5 ms plus
    // PTime microseconds.
    // =============================================================================
    CFX_SIPS_PTIME          = 12,
    CFX_SIPS_PORT_OFFSET_MS = 13,
    CFX_SIPS_PBEND          = 14,
    CFX_SIPS_LOW_KEY        = 15,
    CFX_SIPS_HIGH_KEY       = 16,
    CFX_SIPS_PEDAL          = 17
};

// =============================================================================
// Ordered so that a controller moves the mode as the script's did: 0 is off,
// the bottom half Solo, the top half Legato, and 127 Portamento.
// =============================================================================
enum SipsMode {
    SIPS_MODE_OFF        = 0,
    SIPS_MODE_SOLO       = 1,
    SIPS_MODE_LEGATO     = 2,
    SIPS_MODE_PORTAMENTO = 3
};

enum SipsReleaseMode {
    SIPS_RLS_KNOB     = 0,
    SIPS_RLS_KEY_LIFT = 1
};

enum SipsOffsetMode {
    SIPS_OFFSET_AUTO_PRIOR  = 0,
    SIPS_OFFSET_AUTO_PHRASE = 1,
    SIPS_OFFSET_KNOB        = 2,
    SIPS_OFFSET_KNOB_RANDOM = 3
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
    BUS_FX_GAIN = 1,
    BUS_FX_CHORUS = 2,
    BUS_FX_DELAY = 3,
    BUS_FX_LP1 = 4,
    BUS_FX_LP2 = 5,
    BUS_FX_HP2 = 6,
    BUS_FX_BP2 = 7,
    BUS_FX_PEAK = 8,
    BUS_FX_LOW_SHELF = 9,
    BUS_FX_HIGH_SHELF = 10,
    BUS_FX_NOTCH = 11,
    BUS_FX_REVERB = 12,
    BUS_FX_PHASER = 13,
    BUS_FX_PITCH_SHIFT = 14,
    BUS_FX_WAVE_FOLDER = 15,
    BUS_FX_WAVE_SHAPER = 16,
    BUS_FX_STEREO_SIMULATOR = 17,
    BUS_FX_BIT_CRUSHER = 18,
    BUS_FX_COMPRESSOR = 19,
    BUS_FX_STUTTER = 20,
    BUS_FX_CONVOLUTION = 21
};

// Bus and instrument parameters start at zero. Collection parameters use
// these same slots plus one, after Strength. Time and stereo offset are seconds.
enum ChorusParams { CHORUS_MIX = 0, CHORUS_DEPTH = 1, CHORUS_RATE = 2, CHORUS_PARAM_COUNT = 3 };

// =============================================================================
// With Sync on, the delay follows the tempo: Note is an index into the note
// values from a 1/64 triplet to five bars, and Time is not read.
// =============================================================================
enum DelayParams {
    DELAY_TIME = 0, DELAY_FEEDBACK = 1, DELAY_WET = 2,
    DELAY_STEREO_OFFSET = 3, DELAY_CUTOFF = 4,
    DELAY_SYNC = 5, DELAY_NOTE = 6, DELAY_PARAM_COUNT = 7
};
enum BFX_FilterParams { BFX_FILTER_FREQUENCY = 0, BFX_FILTER_Q = 1, BFX_FILTER_GAIN_DB = 2 };

// =============================================================================
// The collection notch uses the filter slots, cutoff and resonance, which are
// these plus one.
// =============================================================================
enum NotchParams { NOTCH_FREQUENCY = 0, NOTCH_Q = 1, NOTCH_PARAM_COUNT = 2 };

// =============================================================================
// Room size, damping, wet, dry and width run from 0 to 1.
// =============================================================================
enum ReverbParams {
    REVERB_ROOM_SIZE = 0, REVERB_DAMPING = 1, REVERB_WET = 2,
    REVERB_DRY = 3, REVERB_WIDTH = 4, REVERB_PARAM_COUNT = 5
};

// =============================================================================
// Rate and centre are in Hz, feedback runs from -1 to 1.
// =============================================================================
enum PhaserParams {
    PHASER_MIX = 0, PHASER_DEPTH = 1, PHASER_RATE = 2,
    PHASER_CENTER = 3, PHASER_FEEDBACK = 4, PHASER_PARAM_COUNT = 5
};

enum PitchShiftParams { PITCH_SHIFT_SEMITONES = 0, PITCH_SHIFT_MIX = 1, PITCH_SHIFT_PARAM_COUNT = 2 };

enum WaveFolderParams { WAVE_FOLDER_DRIVE = 0, WAVE_FOLDER_THRESHOLD = 1, WAVE_FOLDER_PARAM_COUNT = 2 };

// =============================================================================
// Output is a linear gain. High quality is a switch, 0 or 1.
// =============================================================================
enum WaveShaperParams {
    WAVE_SHAPER_DRIVE = 0, WAVE_SHAPER_BOOST = 1, WAVE_SHAPER_OUTPUT = 2,
    WAVE_SHAPER_HIGH_QUALITY = 3, WAVE_SHAPER_PARAM_COUNT = 4
};

// =============================================================================
// The algorithm is stored by number. Delay is in seconds; rate and depth
// apply to double tracking only.
// =============================================================================
enum StereoSimulatorParams {
    STEREO_SIM_ALGORITHM = 0, STEREO_SIM_WIDTH = 1, STEREO_SIM_DELAY = 2,
    STEREO_SIM_RATE = 3, STEREO_SIM_DEPTH = 4, STEREO_SIM_PARAM_COUNT = 5
};
enum StereoSimAlgorithm { STEREO_SIM_LAURIDSEN = 0, STEREO_SIM_SCHROEDER = 1, STEREO_SIM_ADT = 2 };

enum BitCrusherParams {
    BIT_CRUSHER_BITS = 0, BIT_CRUSHER_DOWNSAMPLE = 1, BIT_CRUSHER_MIX = 2, BIT_CRUSHER_PARAM_COUNT = 3
};

// =============================================================================
// The bus and instrument compressor. Levels are in dB, times in milliseconds,
// auto bypass is a switch. The collection compressor has its own layout.
// =============================================================================
enum CompressorParams {
    COMPRESSOR_THRESHOLD = 0, COMPRESSOR_RATIO = 1, COMPRESSOR_ATTACK = 2,
    COMPRESSOR_RELEASE = 3, COMPRESSOR_INPUT_GAIN = 4, COMPRESSOR_OUTPUT_GAIN = 5,
    COMPRESSOR_AUTO_BYPASS = 6, COMPRESSOR_PARAM_COUNT = 7
};

enum StutterParams { STUTTER_AMOUNT = 0, STUTTER_MIX = 1, STUTTER_PARAM_COUNT = 2 };

// =============================================================================
// The impulse response is a position in the instrument's list of impulse
// response files. Convolution is a bus and instrument effect only: on a
// collection every voice would run a convolution of its own.
// =============================================================================
enum ConvolutionParams { CONVOLUTION_MIX = 0, CONVOLUTION_IR = 1, CONVOLUTION_PARAM_COUNT = 2 };

#define DELAY_NOTE_COUNT 25
#define STEREO_SIM_ALGORITHM_COUNT 3

enum BFX_GainParams {
    BFX_GAIN_LEVEL_DB = 0,
    BFX_GAIN_LAG_MS   = 1
};

#define MAX_FX_PER_COLLECTION   8

#define MAX_DRIVERS_PER_FX      8

#define MAX_FX_PARAMS           32

#define COLLECTION_FX_TYPE_COUNT 39

#define CFX_STRENGTH 0

#define CFX_SV_PARAM_COUNT_1POLE 2

#define CFX_SV_PARAM_COUNT       3

#define CFX_PANNER_PARAM_COUNT (CFX_PAN_SHUFFLE_DB + 1)

#define CFX_COMPRESSOR_PARAM_COUNT (CFX_COMP_SIDECHAIN_HZ + 1)

#define CFX_SMART_RELEASE_PARAM_COUNT (CFX_SREL_ATTACK_BEND + 1)

#define CFX_SIMULATED_LEGATO_PARAM_COUNT (CFX_SIMLEG_RELEASES + 1)

#define SIMLEG_FAMILY_COUNT         3
#define SIMLEG_PROFILES_PER_FAMILY  4
#define SIMLEG_STYLE_COUNT          3
#define SIMLEG_REPEAT_COUNT         3
#define SIMLEG_SAMPLED_POLICY_COUNT 2

#define CFX_SIPS_LEGATO_PARAM_COUNT (CFX_SIPS_PEDAL + 1)

#define SIPS_MODE_COUNT         4
#define SIPS_RLS_MODE_COUNT     2
#define SIPS_OFFSET_MODE_COUNT  4

#define SREL_MAX_MATCH_DB      60.0f

#define SREL_DEFAULT_MATCH_DB  40.0f

#define SREL_MAX_OFFSET_DB     24.0f

#define SREL_MAX_ATTACK_PCT   200.0f

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

#define BUS_FX_TYPE_COUNT 22
