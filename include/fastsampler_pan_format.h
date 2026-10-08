// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
#pragma once

namespace Steinberg { namespace Vst {

enum PanMode {
    PAN_MODE_LEVEL    = 0,
    PAN_MODE_SPECTRAL = 1,
    PAN_MODE_WIDTH    = 2
};

enum PanLaw {
    PAN_LAW_3DB   = 0,
    PAN_LAW_2_5DB = 1,
    PAN_LAW_4_5DB = 2,
    PAN_LAW_6DB   = 3
};

enum PanImage {
    PAN_IMAGE_BALANCE = 0,
    PAN_IMAGE_PAN     = 1
};

} }

#define PAN_MODE_COUNT 3

#define PAN_LAW_COUNT 4

#define PAN_IMAGE_COUNT 2

#define PAN_TILT_DEFAULT_HZ  1000.0f

#define PAN_TILT_DEFAULT_DB    12.0f

#define PAN_SHUFFLE_DEFAULT_HZ   700.0f

#define PAN_WIDTH_DEFAULT 1.0f

#define PAN_SHUFFLE_DEFAULT_DB 0.0f
