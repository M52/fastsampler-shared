# Shareable instruments

A `.fsb` holds sample audio, often from a commercial library, so it cannot be
published. A `.fsi` holds no audio. This design lets an author publish an
edited `.fsi`, and lets anyone who owns the same `.gig` files rebuild its
`.fsb` with fsbanktool.

Status: in progress. The records, the steps and their tests are in this
repository from release 1.1.0. Support in fsbanktool and FastSampler follows.

## Example

1. fsbanktool converts `Violins P.gig`, `Violins MP.gig` and `Violins F.gig`.
2. FastSampler loads the three `.fsi` files, and "Transfer all to working
   instrument" moves their collections into one instrument. Collection names
   can collide or be renamed, so nothing below depends on them.
3. The author edits and deletes zones, adds collection effects, moves start,
   end, in and out points, and exports a new `.fsi` and `.fsb`.
4. The author publishes the `.fsi`. A user opens it in fsbanktool, which asks
   for `Violins P.gig` in a folder named `Violins`, and for the other files
   that the remaining zones use. fsbanktool then writes the `.fsb`.

## Records in the FSI

The `.fsi` gains optional records that say where each stored sample comes from
and how its audio was made. They mirror the FSB v2 sample table without the
audio.

Per instrument, in `FSIFile`:

- `bank_sample_rate` and `bank_channels`: the format of the bank. Every frame
  position in the `.fsi` counts in bank frames.
- `source_files`, up to 256: the file name, the name of its parent folder, and
  the size and hash of each `.gig` and of each extension file it uses. An
  extension file names its `.gig` in `extension_of`. No entry holds a full
  path.
- `source_samples`, up to 4096 stored samples.

Per stored sample, in `FSISourceSample`:

- `file` and `sample`: the zero-based source file entry, and the zero-based
  number of the sample inside that `.gig`.
- `source_fingerprint`.
- `steps`, up to 8, in order.
- `bits` and `frames`: the depth and length of the result.
- `result_fingerprint`.

Per zone, in `FSIZone`:

- `source_sample`: the one-based stored sample that the zone plays. Zero or
  absent means that the zone has no source.
- `source_first` and `source_frames`: the window of the zone in the result, as
  in an FSB row.

Several zones can share one stored sample, so the records sit on stored samples
and zones point at them.

## Steps

`fastsampler_source_format.h` holds the step IDs, and `fastsampler_source.h`
declares the code that applies them.

| ID | Step | Parameters | Added by |
| --- | --- | --- | --- |
| 1 | Channels | `channels` | fsbanktool conversion |
| 2 | Depth, rounded half up | `bits` | fsbanktool conversion |
| 3 | Resample, windowed sinc | `rate`, `bits` | fsbanktool conversion |
| 4 | Resample, cubic | `rate`, `bits` | FastSampler export |
| 5 | Depth from float, rounded half to even | `bits` | FastSampler export |
| 6 | Cut | `first`, `frames` | FastSampler export |

fsbanktool rounds resampled audio straight to the bank depth, so a resample
step carries its output depth and needs no depth step after it. The two depth
steps round differently, and each program has its own. Storing 16-bit audio in
a 24-bit bank loses nothing and is not a step.

FastSampler's export stores each sample from the first frame that one of its
zones uses to the last. With "Trim samples" on, a zone uses its in to out
range. With it off, a zone uses its whole window. When that range is shorter
than the sample, the export adds a cut.

A rebuild replays the steps in order, because the order changes the result.
The export resamples a sample before it cuts it. The cubic resampler starts its
frame grid at the first frame of its input and clamps at the edges of its
input. Cutting first and resampling second therefore gives other frame counts
and other audio. That order occurs when an author exports with trimming, loads
that `.fsi` and exports it again at another rate.

A step ID identifies one exact behaviour, including how it computes frame
counts, for as long as published `.fsi` files exist. A changed algorithm gets a
new ID, and the old one stays available.

The step code lives in this repository. FastSampler and fsbanktool call it and
keep no copies. The tests compare each step with a copy of the code it came
from, and pin its output with a fixed input and an expected fingerprint, so a
change that alters a step fails the tests. Consumers compile the step code with
the floating-point settings in CONSUMERS.md, so both programs produce
bit-identical audio.

## Fingerprints

A fingerprint is XXH64 with seed 0 over interleaved sample values. Each value
is written as the four little-endian bytes of a 32-bit integer at the depth of
the audio. The decoder gives 8-bit samples at the 16-bit scale.

The source fingerprint covers the decoded frames of the `.gig` sample at its
own bit depth and channel count, before any step. An edition that stores the
same audio compressed gives the same fingerprint.

The result fingerprint covers the audio after the last step, at the depth in
`bits`. It does not depend on the bit depth the bank stores. A rebuild can
store the audio at more bits than the steps produce, but not at fewer.

A file hash is XXH64 with seed 0 over the bytes of the file.

A 64-bit non-cryptographic hash is enough. The fingerprints catch mistakes,
such as a different file with the same name. They do not protect against
tampering.

## Rebuild checks

| Check | Meaning | What fsbanktool does |
| --- | --- | --- |
| Size and hash match | Same file | Continues |
| Hash differs, source fingerprints match | Other edition with the same samples | Continues and reports it |
| A source fingerprint differs | That sample is different | Builds and warns |
| Source fingerprint matches, result fingerprint differs | The steps did not repeat exactly | Builds and warns that the bank is not bit-identical |

Before it warns about a different sample, fsbanktool looks for the fingerprint
in the rest of the file, because another edition can store the samples in
another order. A warning names the file, the sample, and the zones and
collections that play it. When a sample is shorter than a cut or a window
needs, fsbanktool pads it with silence and gives a stronger warning.

## FastSampler

- The records of a stored sample stay with the sample table entry that holds
  its audio, because zones already point at that entry. The source files are
  kept per instrument.
- "Transfer all to working instrument" copies the records with the entries and
  merges the source files. Unlocking an instrument replaces its entries and
  carries the records over.
- Loading and saving a `.fsi` reads and writes the records. A record whose
  window does not match the bank is dropped on load.
- The export adds the resample, depth and cut steps it applies, and computes
  the result fingerprints from the audio it stores.
- The export dialog shows "Shareable", or "Not shareable" with the number of
  zones that have no source and the collections they are in. An "Include
  source records" checkbox is on by default, because records cannot be added
  later without the original `.fsi` files. When only some zones have a source,
  the export still writes their records.
- Zones added from WAV files have no source.

## fsbanktool

Conversion:

- Makes each bank sample by replaying the shared steps, so that conversion and
  rebuild run the same code, and writes the records. Each window covers the
  whole sample.
- Switches FMA3 off at start, as CONSUMERS.md requires.
- Offers an option to store every instrument in stereo. FastSampler does not
  export an instrument whose zones have different channel counts, so layers
  from a mono `.gig` and a stereo `.gig` need it to share one instrument.
- The verifier also checks the records.

Rebuild:

- Opens a `.fsi` and lists the `.gig` files that its zones use, with the folder
  name and the status of each file. A file that no zone uses is not requested.
- Asks the user to locate each file, or searches a library folder by folder
  name, file name and size.
- Decodes only the samples that the zones use, replays their steps and runs
  the checks above.
- Writes the `.fsb` under the name the `.fsi` gives. Each row takes the name of
  its zone, because FastSampler relinks a bank by row name. The user chooses
  preload, compression and bit depth.

## Compatibility

- Every new field is optional. A `.fsi` without records loads as before and is
  not shareable.
- Older FastSampler builds skip the new fields, and their exports write no
  records.
- Instruments converted before this change have no records and must be
  converted again.
- FastResampler renders have no `.gig` source. FastResampler only updates its
  submodule.
- The schema change and the consumer updates follow CONSUMERS.md.

## Open details

1. Whether FastSampler generates FSP peak caches and SMFP fingerprints again
   for a rebuilt bank, because those files are not published either.
