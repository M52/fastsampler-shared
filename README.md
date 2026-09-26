# FastSampler shared code

Code shared by FastSampler, FastResampler and fsbanktool: file-format
definitions, their serialization, and code whose results must be identical in
more than one of these programs.

| Format | Purpose | Current representation |
| --- | --- | --- |
| FSI | Instruments, zones, effects, routing and sample bank manifests | Protobuf, `FSIP` magic, 32-bit payload size |
| FSB | Sample banks and shared-sample metadata | Packed little-endian v2; v1 reader retained |
| FSP | Waveform peak caches | Protobuf, `FSPP` magic, 32-bit payload size |
| SMFP | Spectral morph fingerprints | Fixed little-endian v4 records |

## Integration

`include/` contains schemas, manually maintained nanopb bindings, public
headers, persisted effect IDs, step IDs, parameter slots and defaults. `src/`
contains the standalone FSB validation, FSP peak-cache I/O and SMFP I/O
implementations, the spectral analysis that SMFP fingerprints are measured
with, the processing steps and hash of FSI source records, and the checks
and digests of FSI sample banks.
FSI mappings to each application's objects stay in that application.

Add `include` and `3rdparty/nanopb` to your include paths, and define
`PB_FIELD_32BIT` consistently for every nanopb translation unit. Compile the
required source files, `include/fastsampler.pb.c`, and the nanopb runtime once.
A program that records or replays processing steps also compiles
`src/fastsampler_source.cpp` and `src/fastsampler_hash.cpp`, and follows the
floating-point rules in [CONSUMERS.md](CONSUMERS.md). A program that writes
or compares spectral fingerprints also compiles
`src/fastsampler_smfp_analysis.cpp`. A program that reads or writes sample
bank manifests also compiles `src/fastsampler_banks.cpp`,
`src/fastsampler_fsb.cpp` and `src/fastsampler_hash.cpp`.

[SHAREABLE-INSTRUMENTS.md](SHAREABLE-INSTRUMENTS.md) describes the source
records, which let fsbanktool rebuild the `.fsb` of a published `.fsi` from the
user's own `.gig` files.

## Sample banks

From release 1.2.0, one `.fsi` can name up to eight `.fsb` sample banks, for
example one bank for each microphone position. `fastsampler_banks.h` declares
the checks and the digests. The `FS_BANKS_ERR_*` codes name each rule.

### Shape

- **A file without banks** (`banks` is absent) has one implicit bank with ID
  1 (`FSI_BANK_ID_IMPLICIT`). This bank is the file that `fsb_path` names,
  beside the `.fsi`, and its row z holds zone z. The bank can have more rows
  than the file has zones. No zone gives `bank` or `bank_row`. When the file
  gives `next_bank_id`, the value is 2 or more.
- **A file with banks** has 1 to 8 (`FSI_MAX_BANKS`) entries in `banks`:
  - Every zone gives `bank`, a zero-based index into `banks`, and `bank_row`.
  - Every row of every bank belongs to exactly one zone.
  - All zones of one collection are in one bank. A collection without zones
    has no bank.
  - `fsb_path` is the file name of the `.fsi` itself, without a folder.
  - `mixer_revision` is 3 (`FSI_MIXER_REVISION_BANKS`) or more.
  - `bank_sample_rate` and `bank_channels` are absent.
  - Only the zones of one bank use a stored sample (`source_sample`).
  - `next_bank_id` is above every bank ID.
  - Collection ranges and FSP peak rows count zones in file order.
- **A bank entry** has a `stable_id` that is not 0 and that no other bank has,
  an `fsb_path` that is not empty, `row_count` 1 to 4096, `sample_rate`,
  `channels` 1 or 2, `bps` 16 or 24, and both digests. When it gives
  `authoring_bus`, the bus is below 8 and no other bank gives it. All banks
  have the same sample rate. The channel count and bit depth can be different
  for each bank. The source records of the zones of a bank have the format of
  that bank.
- **The digests** are XXH64 with seed 0. `table_digest` covers the header and
  the rows as the bank stores them: 44 bytes, plus 104 bytes for each row of a
  version 1 bank or 128 bytes for each row of a version 2 bank.
  `preload_digest` covers the whole frames of the preload block, from
  `preload_offset`. The table digest includes the stream offsets, so a bank
  that is compressed again does not match its `.fsi`.

### Writers

1. Call `fs_banks_check_file` before you write. When it reports a problem,
   write nothing.
2. Write a file without banks only for one bank with ID 1 whose row z holds
   zone z. Otherwise write banks, also for one bank.
3. Keep the ID of each bank that you write again. A new bank gets
   `next_bank_id`, and `next_bank_id` then increases by one. It never
   decreases. When you remove a bank, do not change the IDs of the other
   banks.
4. Write a bank path relative to the folder of the `.fsi`, with `/` and `..`
   parts, when the bank and the `.fsi` are on one root. Otherwise write an
   absolute path. Calculate the path for the final location of the `.fsi`.
5. Do not write a bank path that only one project or one computer uses.
6. Write `mic_copy_of` only when you also write the collection that it names.

### Readers

1. Refuse a `mixer_revision` above the newest revision that you read
   (`FSI_MIXER_REVISION_NEWEST`).
2. Refuse the file when `fs_banks_check_file` reports a problem.
3. A reader that cannot put zones in banks refuses a file with banks. It never
   plays one bank as the whole instrument.
4. Check each bank with `fs_banks_check_bank` before you use it, and compare
   the preload digest while you read the preload. A missing or failed bank
   makes its zones silent. Keep its zones, the collections, the other data and
   the other banks.
5. Ignore `mic_copy_of`, as you ignore `release_of`, when the collection it
   names is absent, is the same collection, or is a copy itself.

A reader accepts a relative bank path with `/` or `\`, resolves it against
the folder of the `.fsi`, and removes `.` and `..` parts.

The bank checks read files and hash audio. Run them on a worker thread when
you load an instrument, restore a bank or add a bank, and never on the audio
thread.

### Older programs

| Program | Result with a file with banks |
| --- | --- |
| FastSampler from build 699, before bank support (public 1.2.0.706 included) | Refuses revision 3: "Could not read FSI file." |
| FastSampler build 698 and older | Maps the `.fsi` itself as its bank. The `FSIP` magic fails the bank header check: "Could not read headers from mapped FSB file." |
| fsbanktool with shared code before 1.2.0, rebuild | Finds no bank format (tags 17 and 18) and refuses before it writes |
| fsbanktool with shared code before 1.2.0, inspect | Reports that the file is not an `.fsb` sound bank |
| FastResampler | Reads no `.fsi` |

## Format changes

Make shared format changes here. Keep `.proto`, `.options` and `.pb.h/.c`
consistent. Do not regenerate the maintained bindings without checking their
custom capacities and callbacks. Preserve field tags and numeric IDs. Version
incompatible binary layouts explicitly and retain readers for supported old
versions. Update the pinned submodule in all affected consumers after testing.
See [CONSUMERS.md](CONSUMERS.md) for the update procedure.

On Windows with Visual Studio C++ Build Tools, run `tests\run-tests.bat`.
GitHub Actions runs these tests on pushes and pull requests. Consumer integration
tests remain necessary whenever schemas or their meaning change.

[GitLab](https://git.omkserver.nl/Macaberz/fastsampler-shared) is the upstream
repository; [GitHub](https://github.com/M52/fastsampler-shared) is the public mirror.
From a clean `main` checkout, `scripts/mirror.ps1` pushes the branch and version
tags to upstream first, then GitHub.

First-party code: [MIT](LICENSE). Nanopb: [zlib](3rdparty/nanopb/LICENSE.txt).
