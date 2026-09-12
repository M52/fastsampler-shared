# FastSampler formats

Public, MIT-licensed file-format contract shared by FastSampler, FastResampler
and fsbanktool. No sampler engine, plugin host or application UI is included.

| Format | Purpose | Current representation |
| --- | --- | --- |
| FSI | Instruments, zones, effects and routing | Protobuf, `FSIP` magic, 32-bit payload size |
| FSB | Sample banks and shared-sample metadata | Packed little-endian v2; v1 reader retained |
| FSP | Waveform peak caches | Protobuf, `FSPP` magic, 32-bit payload size |
| SMFP | Spectral morph fingerprints | Fixed little-endian v4 records |

`include/` contains schemas, manually maintained nanopb bindings, public
headers, persisted effect IDs, parameter slots and defaults. `src/` contains
the standalone FSB validation, FSP peak-cache I/O and SMFP I/O implementations.
FSI mappings to each application's objects stay in that application.
`3rdparty/nanopb` contains the existing zlib-licensed runtime.

Add `include` and `3rdparty/nanopb` to your include paths, and define
`PB_FIELD_32BIT` consistently for every nanopb translation unit. Compile the
required source files, `include/fastsampler.pb.c`, and the nanopb runtime once.
All consumers initially pin the same 1.0.0 extraction. This changes ownership
and build inputs, not the existing file bytes or version numbers.

## Tests

On Windows with Visual Studio C++ Build Tools: `tests\run-tests.bat`.
The standalone tests cover fixed record sizes, known protobuf wire fields,
FSB v1/v2 reading, invalid bank headers, and FSP/SMFP roundtrips. GitHub Actions
runs them on pushes and pull requests. Consumer integration tests remain
necessary whenever schemas or their meaning change.

## Ownership and releases

Upstream: https://git.omkserver.nl/Macaberz/fastsampler-formats

Public mirror: https://github.com/M52/fastsampler-formats

Use `scripts/mirror.ps1` from a clean `main` checkout to push main and version
tags to upstream first, then GitHub. Like FastSampler, mirroring uses ordinary
explicit pushes; it is not a background synchronization service.

Make shared format changes here. Keep `.proto`, `.options` and `.pb.h/.c`
consistent. Do not regenerate the maintained bindings without checking their
custom capacities and callbacks. Preserve field tags and numeric IDs. Version
incompatible binary layouts explicitly and retain readers for supported old
versions. Update the pinned submodule in all affected consumers after testing.
See [CONSUMERS.md](CONSUMERS.md) for the update procedure.

First-party code: [MIT](LICENSE). Nanopb: [zlib](3rdparty/nanopb/LICENSE.txt).
