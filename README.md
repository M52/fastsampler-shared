# FastSampler file formats

This repository defines four file formats and the code that reads, writes and
checks them.

| Format | Contents | Encoding |
| --- | --- | --- |
| FSI | Instrument: zones, collections, effects, routing, sample bank manifests, source records | Protobuf with an `FSIP` header |
| FSB | Sample bank: a table of rows, a preload block of PCM frames, and WavPack streams | Packed binary, version 2; version 1 is still read |
| FSP | Waveform peak cache, one entry per zone | Protobuf with an `FSPP` header |
| SMFP | Spectral fingerprints of the zones of one collection | Fixed binary records, version 4 |

All binary values are little-endian. The format release is
`FASTSAMPLER_FORMATS_RELEASE` in `include/fastsampler_formats.h`, currently
1.2.0. It is independent of the version numbers inside the files.

## Using the code

`include/` holds the schema, the nanopb bindings, the headers, and the
persisted effect IDs, parameter slots, defaults and step IDs. `src/` holds the
FSB checks, FSP and SMFP input and output, the spectral analysis for SMFP, the
processing steps and hash for source records, and the checks and digests for
sample banks.

Add `include` and `3rdparty/nanopb` to the include paths, and define
`PB_FIELD_32BIT` for every translation unit that uses nanopb. Compile
`include/fastsampler.pb.c` and the nanopb runtime once. Then compile the
sources you need:

| You read or write | Compile |
| --- | --- |
| FSB headers | `src/fastsampler_fsb.cpp` |
| FSP | `src/fastsampler_fsp.cpp` |
| SMFP | `src/fastsampler_smfp.cpp` |
| SMFP measurements | `src/fastsampler_smfp_analysis.cpp` |
| FSI with sample banks | `src/fastsampler_banks.cpp`, `src/fastsampler_fsb.cpp`, `src/fastsampler_hash.cpp` |
| FSI source records and steps | `src/fastsampler_source.cpp`, `src/fastsampler_hash.cpp` |

The nanopb bindings are maintained by hand. `include/fastsampler.options` sets
the capacities of the repeated fields and strings it lists, and a file that
exceeds one of them does not decode.

## Protobuf files: FSI and FSP

An FSI or FSP file has an 8-byte header followed by a protobuf payload:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic: `FSIP` (0x50495346) or `FSPP` (0x50505346) |
| 4 | 4 | Payload size in bytes, not 0 |
| 8 | size | `FSIFile` or `FSPFile`, as defined in `include/fastsampler.proto` |

The schema is proto2. Its comments define each field. Field tags never change
meaning.

## FSI

An FSI file describes one instrument. Each zone maps a row of an FSB sample
bank to a note and velocity range, with its envelope, loop and playback
settings. Collections group zones and carry effects, modulators and routing.
The audio stays in the banks.

`mixer_revision` records the mixer schema the file was written with. Absence
means the file predates the output mixer, revision 2 adds aux sends, and
revision 3 marks a file with sample banks. A reader refuses a revision above
`FSI_MIXER_REVISION_NEWEST`.

### Sample banks

One FSI file can name up to eight FSB sample banks, for example one per
microphone position. `include/fastsampler_banks.h` declares the checks and the
digests. The `FS_BANKS_ERR_*` codes name each rule.

A file without banks (`banks` is absent) has one implicit bank with ID 1
(`FSI_BANK_ID_IMPLICIT`). This bank is the file that `fsb_path` names, beside
the `.fsi`, and its row z holds zone z. The bank can have more rows than the
file has zones. No zone gives `bank` or `bank_row`. When the file gives
`next_bank_id`, the value is 2 or more.

A file with banks has 1 to 8 (`FSI_MAX_BANKS`) entries in `banks`:

- Every zone gives `bank`, a zero-based index into `banks`, and `bank_row`.
- Every row of every bank belongs to exactly one zone.
- All zones of one collection are in one bank. A collection without zones has
  no bank.
- The range of each collection with zones ends at or before the last zone, and
  each zone is in the range of a collection.
- `fsb_path` is the file name of the `.fsi` itself, without a folder. A reader
  that does not know banks then finds no bank header and refuses the file.
- `mixer_revision` is 3 (`FSI_MIXER_REVISION_BANKS`) or more.
- `bank_sample_rate` and `bank_channels` are absent.
- Only the zones of one bank use a stored sample (`source_sample`).
- `next_bank_id` is above every bank ID.
- Collection ranges and FSP peak rows count zones in file order.

A bank entry has a `stable_id` that is not 0 and that no other bank has, an
`fsb_path` that is not empty, `row_count` 1 to 8192, `sample_rate`, `channels`
1 or 2, `bps` 16 or 24, and both digests. When it gives `authoring_bus`, the
bus is below 8 and no other bank gives it. All banks have the same sample
rate. The channel count and bit depth can differ between banks. The source
records of the zones of a bank have the format of that bank.

A bank entry can give `purged`: the user had the bank out of RAM when the file
was saved. A player opens the file with that bank purged until the user loads
it, and loads one bank when every bank is purged. Absent means false. A reader
that does not know `purged` loads every bank.

The digests are XXH64 with seed 0. `table_digest` covers the FSB header and
rows as the bank stores them: 44 bytes, plus 104 bytes for each row of a
version 1 bank or 128 bytes for each row of a version 2 bank. `preload_digest`
covers the whole frames of the preload block, from `preload_offset`. The table
digest includes the stream offsets, so a bank that is compressed again does not
match its `.fsi`.

Writers:

1. Call `fs_banks_check_file` before you write. When it reports a problem,
   write nothing.
2. Write a file without banks only for one bank with ID 1 whose row z holds
   zone z. Otherwise write banks, also for one bank.
3. Keep the ID of each bank that you write again. A new bank gets
   `next_bank_id`, and `next_bank_id` then increases by one. It never
   decreases. When you remove a bank, do not change the IDs of the other banks.
4. Write a bank path relative to the folder of the `.fsi`, with `/` and `..`
   parts, when the bank and the `.fsi` are on one root. Otherwise write an
   absolute path. Calculate the path for the final location of the `.fsi`.
5. Do not write a bank path that only one project or one computer uses.
6. Write `mic_copy_of` only when you also write the collection that it names.

Readers:

1. Refuse the file when `fs_banks_check_file` reports a problem.
2. A reader that cannot put zones in banks refuses a file with banks. It never
   plays one bank as the whole instrument.
3. Check each bank with `fs_banks_check_bank` before you use it, and compare
   the preload digest while you read the preload. A missing or failed bank
   makes its zones silent. Keep its zones, the collections, the other data and
   the other banks.
4. Ignore `mic_copy_of`, as you ignore `release_of`, when the collection it
   names is absent, is the same collection, or is a copy itself.
5. Accept a relative bank path with `/` or `\`, resolve it against the folder
   of the `.fsi`, and remove `.` and `..` parts.

The bank checks read files and hash audio. Run them on a worker thread, never
on an audio thread.

### Source records

Source records say where each stored sample comes from and how its audio was
made, so that a bank can be rebuilt from the original source files. They mirror
the FSB version 2 sample table without the audio. Every source field is
optional. A file without them loads as before and cannot be rebuilt.

In `FSIFile`:

- `bank_sample_rate` and `bank_channels`: the format of the bank. Every frame
  position in the `.fsi` counts in bank frames. A file with banks leaves them
  out, and the records of a zone have the format of its bank.
- `source_files`, up to 256: the file name, the name of its parent folder, and
  the size and hash of each source file. An extension file names its main file
  in `extension_of`. No entry holds a full path.
- `source_samples`, up to 8192 stored samples.

In `FSISourceSample`:

- `file` and `sample`: the zero-based source file entry, and the zero-based
  position of the sample in that file.
- `source_fingerprint`.
- `steps`, up to 8, in order.
- `bits` and `frames`: the depth and length of the result.
- `result_fingerprint`.

In `FSIZone`:

- `source_sample`: the one-based stored sample that the zone plays. Zero or
  absent means that the zone has no source.
- `source_first` and `source_frames`: the window of the zone in the result, as
  in an FSB row.

Several zones can share one stored sample, so the records sit on stored
samples and zones point at them. In a file with banks, a stored sample belongs
to one bank, and all banks share the source files. A rebuild makes each bank
from the records of its zones, in bank row order.

### Processing steps

`include/fastsampler_source_format.h` holds the step IDs, and
`include/fastsampler_source.h` declares the code that applies them.

| ID | Step | Parameters |
| --- | --- | --- |
| 1 | Channels: copy mono to both channels, or keep the first two | `channels` |
| 2 | Integer depth conversion, rounded half up | `bits` |
| 3 | Resample, windowed sinc, rounded to the output depth | `rate`, `bits` |
| 4 | Resample, cubic on float audio, quantized to the output depth | `rate`, `bits` |
| 5 | Depth from float, rounded half to even | `bits` |
| 6 | Cut: keep `frames` frames from `first` | `first`, `frames` |

A resample step carries its output depth and needs no depth step after it.
Storing 16-bit audio in a 24-bit bank loses nothing and is not a step.

A rebuild replays the steps in order, because the order changes the result.
The cubic resampler starts its frame grid at the first frame of its input and
clamps at the edges of its input, so a cut followed by a resample gives other
frame counts and other audio than a resample followed by a cut.

A step ID names one exact behaviour, including how it counts frames and rounds
values. A changed algorithm gets a new ID, and the old ID keeps its behaviour.

Replaying a step must give bit-identical audio on every compiler and platform.
Call the functions in `fastsampler_source.h` rather than a copy of their code,
and compile `src/fastsampler_source.cpp` with these settings:

- Use the default floating-point model, `/fp:precise` for MSVC. Do not use
  `/fp:fast` or `/fp:contract`.
- On x64 Windows, call `_set_FMA3_enable(0)` before the first resample step.
  The sinc step uses the C runtime's `sin` and `cos`, and their FMA3 versions
  give other results.
- The cubic resampler turns contraction off for Clang with a pragma. Do not
  override it with compiler flags.

### Fingerprints and hashes

A fingerprint is XXH64 with seed 0 over interleaved sample values. Each value
is written as the four little-endian bytes of a 32-bit integer at the depth of
the audio. 8-bit source audio is fingerprinted at the 16-bit scale.

The source fingerprint covers the decoded frames of the source sample at its
own bit depth and channel count, before any step. Two copies of the same audio
in different containers give the same fingerprint.

The result fingerprint covers the audio after the last step, at the depth in
`bits`. It does not depend on the bit depth the bank stores. A bank can store
the audio at more bits than the steps produce, but not at fewer.

A file hash is XXH64 with seed 0 over the bytes of the file.
`include/fastsampler_hash.h` declares the hash.

These hashes catch mistakes, such as a different file with the same name. They
do not protect against tampering.

A rebuild compares the records with the source files it is given:

| Check | Meaning |
| --- | --- |
| Size and hash match | Same file |
| Hash differs, source fingerprints match | Another edition of the file with the same samples |
| A source fingerprint differs | That sample is different |
| Source fingerprint matches, result fingerprint differs | The steps did not repeat exactly, and the bank is not bit-identical |

Another edition can store its samples in another order, so look for a
fingerprint in the rest of the file before treating a sample as different.

## FSB

An FSB file holds the audio of one sample bank. It starts with a header and a
table of rows, followed by a preload block and the streams.

Header, 44 bytes:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u32 | `magic`: `FSB1` (0x31425346) |
| 4 | u32 | `version`: 2, or 1 for the older row layout |
| 8 | u32 | `sample_rate`: 8000 to 384000 |
| 12 | u32 | `channels`: 1 or 2 |
| 16 | u32 | `preload_frames`: the preload length the bank was written with. 0xFFFFFFFF means every sample is resident in full |
| 20 | u32 | `zone_count`: the number of rows |
| 24 | u64 | `preload_offset`: file offset of the preload block |
| 32 | u64 | `preload_size`: size of the preload block in bytes |
| 40 | u32 | `bps`: 16 or 24 |

Version 2 row, 128 bytes, `zone_count` times after the header:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | char[64] | `name`: the zone name |
| 64 | u64 | `preload_frame_offset`: first frame of this row in the preload block |
| 72 | u64 | `preload_frame_count`: frames of this row in the preload block |
| 80 | u64 | `total_frames`: frames the zone plays |
| 88 | u64 | `sample_frame_offset`: first frame of the zone in its stored sample |
| 96 | u32 | `sample_index`: the stored sample, below `zone_count` |
| 100 | u32 | `sample_frames`: length of the stored sample |
| 104 | u64 | `stream_offset`: file offset of the stream |
| 112 | u64 | `stream_size`: size of the stream in bytes |
| 120 | u64 | `stream_first_frame`: the frame of the stored sample where the stream starts |

The preload block holds interleaved signed PCM frames: 2 bytes per sample for
16-bit banks, 3 bytes for 24-bit banks. Its frame count is `preload_size`
divided by the frame size, because rows can overlap. A row's first
`preload_frame_count` frames start at frame `preload_frame_offset` of the
block.

A stream is a WavPack stream of the stored sample from `stream_first_frame`
to its end. The rest of a zone, after its preload, comes from the stream.
Several rows can share one stored sample, each with its own window into it.
Rows with the same `sample_index` agree on `sample_frames`, `stream_offset`,
`stream_size` and `stream_first_frame`. A stream of one zone leaves out that
zone's preload. A shared stream holds the complete sample, because its zones
can start at different frames.

A reader refuses a bank when:

- The magic, version, channel count, depth or sample rate is outside the
  values above.
- The preload block, a row or a stream lies outside the file.
- A row's preload lies outside the preload block, or is longer than
  `total_frames`.
- `sample_frame_offset + total_frames` exceeds `sample_frames`, or
  `stream_first_frame` exceeds `sample_frames`.
- A zone that needs its stream starts before `stream_first_frame`.

Version 1 rows are 104 bytes: `name`, then `preload_frame_offset`,
`preload_frame_count`, `stream_offset`, `stream_size` and `total_frames`, each
a u64. Each row is its own stored sample, the preload regions follow each other
without gaps or overlap, and the stream starts after the preload.
`fsb_load_headers` reads both versions into the version 2 row layout.

## FSP

An FSP file caches the waveform peaks of the zones of one FSI file. Each
`FSPZone` holds 1024 (`ZONE_PEAK_CACHE_POINTS`) minimum and 1024 maximum
values, and the zones are in the file order of the FSI zones.

## SMFP

An SMFP file holds the spectral fingerprints of the zones of one collection.
It is a header followed by `zone_count` entries, with no padding.

Header, 80 bytes:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u32 | `magic`: `SFP4` (0x34504653) |
| 4 | u32 | `version`: 4 |
| 8 | u32 | `zone_count` |
| 12 | u32 | `band_count`: 31 |
| 16 | char[64] | `collection_name` |

Entry, 408 bytes:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | char[260] | `file_path` |
| 260 | u32 | `total_frames` |
| 264 | u32 | `sample_rate` |
| 268 | u32 | `analysis_frames`: 0 means a flat fallback fingerprint |
| 272 | i32 | `root_note` |
| 276 | i32 | `low_velocity` |
| 280 | i32 | `high_velocity` |
| 284 | f32[31] | `band_db` |

Band b is a third-octave band centred at 1000 × 2^((b − 17) / 3) Hz, from about
19.7 Hz to 20.2 kHz. The levels are in dB, normalized so that their
energy-weighted mean is 0 dB, which keeps level out of the comparison. Version 4
stores levels at absolute frequencies, so fingerprints of captures at
different sample rates compare. A reader refuses any other magic, version or
band count. Version 3 is refused because its FFT bins have no recorded sample
rate.

Measure fingerprints with `smfp_resolve_zone_range`, `smfp_root_note_hz` and
`smfp_analyze_zone_bands` from `include/fastsampler_smfp_analysis.h`. The
comparison uses band levels, so measurements need not match bit for bit.

## Changing a format

- Keep `.proto`, `.options` and the `.pb.h` and `.pb.c` bindings consistent.
  Check the custom capacities and callbacks before you regenerate a binding.
- Never renumber a protobuf field tag, an effect ID, a parameter slot or a
  step ID.
- Give an incompatible binary layout a new version, and keep the reader of
  every supported older version.

`tests\run-tests.bat` builds and runs the tests with Visual Studio C++ Build
Tools.

## License

First-party code: [MIT](LICENSE). nanopb: [zlib](3rdparty/nanopb/LICENSE.txt).
