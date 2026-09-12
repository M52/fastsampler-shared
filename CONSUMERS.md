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
