# FastSampler formats

Shared file-format definitions and serialization code for FastSampler,
FastResampler and fsbanktool.

| Format | Purpose | Current representation |
| --- | --- | --- |
| FSI | Instruments, zones, effects and routing | Protobuf, `FSIP` magic, 32-bit payload size |
| FSB | Sample banks and shared-sample metadata | Packed little-endian v2; v1 reader retained |
| FSP | Waveform peak caches | Protobuf, `FSPP` magic, 32-bit payload size |
| SMFP | Spectral morph fingerprints | Fixed little-endian v4 records |

## Integration

`include/` contains schemas, manually maintained nanopb bindings, public
headers, persisted effect IDs, parameter slots and defaults. `src/` contains
the standalone FSB validation, FSP peak-cache I/O and SMFP I/O implementations.
FSI mappings to each application's objects stay in that application.

Add `include` and `3rdparty/nanopb` to your include paths, and define
`PB_FIELD_32BIT` consistently for every nanopb translation unit. Compile the
required source files, `include/fastsampler.pb.c`, and the nanopb runtime once.

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

[GitLab](https://git.omkserver.nl/Macaberz/fastsampler-formats) is the upstream
repository; [GitHub](https://github.com/M52/fastsampler-formats) is the public mirror.
From a clean `main` checkout, `scripts/mirror.ps1` pushes the branch and version
tags to upstream first, then GitHub.

First-party code: [MIT](LICENSE). Nanopb: [zlib](3rdparty/nanopb/LICENSE.txt).
