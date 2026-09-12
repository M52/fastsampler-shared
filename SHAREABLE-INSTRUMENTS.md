# Shareable instruments

A `.fsb` holds sample audio, often from a commercial library, so it cannot be
published. A `.fsi` holds no audio. This design lets an author publish an
edited `.fsi`, and lets anyone who owns the same `.gig` files rebuild its
`.fsb` with fsbanktool.

Status: design only. Nothing here is implemented.

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

Per instrument:

- The sample rate and channel count of the bank. Every frame position in the
  `.fsi` counts in bank frames.
- A list of source files. An entry holds the file name, the name of its parent
  folder, and the size and hash of the `.gig` and of each extension file it
  uses. No entry holds a full path.

Per stored sample:

- The source file entry and the number of the sample inside that `.gig`.
- The source fingerprint.
- The steps, in order.
- The result fingerprint.

Per zone:

- The stored sample it plays.
- The first frame and the frame count of its window in the result, as in an
  FSB row.

Several zones can share one stored sample, so the records sit on stored samples
and zones point at them.

## Steps

| Step | Parameters | Added by |
| --- | --- | --- |
| To stereo | None | fsbanktool conversion |
| Resample | Target rate, resampler name | fsbanktool conversion (windowed sinc), FastSampler export (cubic) |
| Reduce to 16 bits | Method name | fsbanktool conversion, FastSampler export set to 16 bits |
| Cut | First frame, frame count | FastSampler export |

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

A step name identifies one exact behaviour, including how it computes frame
counts, for as long as published `.fsi` files exist. A changed algorithm gets a
new name, and the old one stays available.

The step code lives in this repository. FastSampler and fsbanktool call it and
keep no copies. Tests pin the output of each step with a fixed input and an
expected fingerprint, so a change that alters a step fails the tests. Consumers
compile the step code with the floating-point settings in CONSUMERS.md, so
both programs produce bit-identical audio.

## Fingerprints

The source fingerprint covers the decoded frames of the `.gig` sample at its
own bit depth and channel count, before any step. An edition that stores the
same audio compressed gives the same fingerprint.

The result fingerprint covers the audio after the last step and does not depend
on the bit depth the bank stores. A rebuild can store the audio at more bits
than the steps produce, but not at fewer.

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

- A zone in memory carries its source, steps and window. "Transfer all to
  working instrument" copies zones whole and needs no change. Undo snapshots
  copy zone fields one at a time and need the new fields.
- Loading and saving a `.fsi` reads and writes the records.
- The export adds the resample and cut steps it applies, and computes the
  result fingerprints from the audio it stores.
- The export dialog shows "Shareable", or "Not shareable" with the number of
  zones that have no source and the collections they are in. An "Include
  source records" checkbox is on by default, because records cannot be added
  later without the original `.fsi` files. When only some zones have a source,
  the export still writes their records.
- Zones added from WAV files have no source.

## fsbanktool

Conversion:

- Writes the records. Each window covers the whole sample, and the steps are
  the ones the conversion applied.
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

1. The fingerprint definition: byte layout, channel order and hash algorithm.
   Like a step name, it cannot change after release.
2. Capacities of the new nanopb arrays: source files per `.fsi` and steps per
   stored sample.
3. Names of the bit reduction methods. fsbanktool truncates 24-bit samples to
   16 bits. The FastSampler export needs its own method name if it converts
   differently.
4. Whether FastSampler generates FSP peak caches and SMFP fingerprints again
   for a rebuilt bank, because those files are not published either.
