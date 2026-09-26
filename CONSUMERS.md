# Shared FastSampler formats

FSI, FSB, FSP and SMFP are maintained in **fastsampler-shared**:

- Upstream: https://git.omkserver.nl/Macaberz/fastsampler-shared
- Public mirror: https://github.com/M52/fastsampler-shared
- Local dependency: `3rdparty/fastsampler-shared` (Git submodule).

The parent commit pins an exact format commit. A normal build uses that
revision and never fetches a moving branch. After cloning or pulling, run:

```text
git submodule update --init --recursive
```

Make schema, file-layout, version, persisted ID, parameter-slot and shared
serialization changes in the separate fastsampler-shared repository. Do not
restore local copies or edit forwarding headers to change a format. The
protobuf schema, options, and maintained nanopb bindings must remain consistent.

To upgrade deliberately, first commit and test the change in fastsampler-shared,
push its commit/tag to the upstream and GitHub mirror, then:

```text
git -C 3rdparty/fastsampler-shared fetch origin --tags
git -C 3rdparty/fastsampler-shared checkout --detach <tested-commit-or-tag>
git add 3rdparty/fastsampler-shared
```

Rebuild FastSampler, FastResampler and fsbanktool against the same revision.
Run the fastsampler-shared tests, FastSampler's full release tests, fsbanktool's
conversion/verification tests, and load generated FSI/FSB pairs through the
FastSampler loader. A successful compile alone does not prove compatibility.
Commit each parent's submodule update and any required adapter changes together.
Updates to one parent's submodule do not change the other parents automatically.

The initial 1.0.0 extraction preserves the existing wire formats: FSB v2
(with the v1 reader), SMFP v4, and the existing FSI/FSP protobuf messages.
Repository release numbers are independent of on-disk version numbers.
Do not renumber existing protobuf fields or persisted effect IDs; new meanings
or incompatible layouts need explicit compatibility handling and versioning.

The shared first-party component is MIT licensed; nanopb retains its zlib
license. FastSampler and FastResampler keep their existing licensing, while
fsbanktool remains GPL-2.0-or-later. Contributions to the shared component must
be suitable for use in both proprietary and GPL applications.

## Source records and processing steps

Release 1.1.0 adds optional source records to FSI: `source_files`,
`source_samples`, `bank_sample_rate` and `bank_channels` in `FSIFile`, and
`source_sample`, `source_first` and `source_frames` in `FSIZone`. Files without
them load as before. See [SHAREABLE-INSTRUMENTS.md](SHAREABLE-INSTRUMENTS.md).

A program that records or replays steps calls the functions in
`fastsampler_source.h` and keeps no copy of their code. Its results must match
the other programs bit for bit:

- Build with the default floating-point model, `/fp:precise` for MSVC. Do not
  use `/fp:fast` or `/fp:contract` for the translation unit that includes
  `fastsampler_source.cpp`.
- On x64 Windows, call `_set_FMA3_enable(0)` before the first resample step.
  The sinc step uses the C runtime's `sin` and `cos`, and their FMA3 versions
  give other results.
- The cubic resampler turns contraction off for Clang with a pragma. Do not
  override it with compiler flags.

## Sample banks

Release 1.2.0 adds sample banks to FSI: `banks` and `next_bank_id` in
`FSIFile`, the `FSIBank` message, `bank` and `bank_row` in `FSIZone`, and
`mic_copy_of` in `FSICollection`. Files without banks load as before. The
rules are in [README.md](README.md#sample-banks).

- A consumer that writes banks calls `fs_banks_check_file` before it writes,
  never writes `bank_sample_rate` or `bank_channels` with banks, and gives
  `fsb_path` the file name of the `.fsi` that it writes.
- A consumer that cannot put zones in banks refuses a file that has them. A
  program that reads FSI files compiles `src/fastsampler_banks.cpp` for these
  checks.
- `FSI_MIXER_REVISION` stays 2 for files without banks. A file with banks has
  `FSI_MIXER_REVISION_BANKS`, and a reader compares the revision with
  `FSI_MIXER_REVISION_NEWEST`.

## Spectral fingerprint analysis

FastSampler measures the collection that carries a spectral morph filter when
it loads it, and compares that measurement with the `.smfp` files of the other
layers. A program that writes or compares fingerprints measures its zones with
`smfp_resolve_zone_range`, `smfp_root_note_hz` and `smfp_analyze_zone_bands`
in `fastsampler_smfp_analysis.h`, and keeps no copy of their code. The SMFP
layout does not change. The filter compares band levels, so measurements need
not match bit for bit, and the floating-point rules for processing steps do
not apply.
