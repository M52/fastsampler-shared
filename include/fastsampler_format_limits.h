// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Matthias
#pragma once
// Persisted sizes and IDs. Change them in fastsampler-shared.
#define MAX_NAME_LENGTH 64
#define MOD_CURVE_POINTS 128
#define ZONE_PEAK_CACHE_POINTS 1024
#define MAX_ZONE_COLLECTIONS 128
#define COLLECTION_FLAG_SOLO       (1 << 0)
#define COLLECTION_FLAG_MUTE       (1 << 1)
#define COLLECTION_FLAG_DISABLED   (1 << 2)
#define COLLECTION_FLAG_REFERENCED (1 << 3)
#define MAX_COLLECTION_LINKS 64
#define COLLECTION_LINK_MAX_INTERVAL 96
#define COLLECTION_LINK_FLAG_PEDAL_UP (1 << 1)
#define COLLECTION_LINK_CC_NONE (-1)
#define COLLECTION_LINK_VEL_MIN 1
#define COLLECTION_ID_NONE 0u
#define FSI_MIXER_REVISION 2u
#define FSI_MIXER_REVISION_OLDEST 1u
// A file with banks carries this revision. Readers of revision 2 refuse it.
#define FSI_MIXER_REVISION_BANKS 3u
// The newest revision that a reader of this release reads.
#define FSI_MIXER_REVISION_NEWEST 3u
#define FSI_MAX_BANKS 8
// The stable ID of the one bank of a file without banks.
#define FSI_BANK_ID_IMPLICIT 1u
// In memory only: a bank that was not built from one output bus. The file
// leaves authoring_bus out.
#define FSI_BANK_BUS_NONE 0xFFu
#define FS_FORMAT_PATH_LENGTH 260
#define COLLECTION_LINK_MAX_RULE 255
#define CFX_RR_MODE_SEQUENCE 0
#define CFX_RR_MODE_RANDOM 1
