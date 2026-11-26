# Spools manual

Spools is an early proof of concept. See `DESIGN.md` for the planned behaviour and `TODO.md` for progress. This manual grows with each milestone.

## Quick start

1. Right-click the module and choose **Load sample…**. Pick a WAV/AIFF/FLAC/MP3/Ogg file or a REX2 loop.
2. Plain audio arrives as one slice. Use **Slice evenly** to cut it until automatic slicing arrives.
3. Patch a gate or trigger into **GATE**. C4 (0 V) on **V/OCT** plays the start slice, and each semitone up or down plays the next or previous slice.
4. Patch a clock into **CLOCK** and set its resolution in the menu. Without one, Spools uses the **BPM** knob, or the loop's own tempo when the knob is at 0.

## Playing slices

- Each trigger plays **LENGTH** slices. Each slice starts on its own beat position, scaled to the current tempo. Pitch is independent of tempo.
- A gate held past the first slice keeps repeating the phrase, up to **LOOP COUNT** passes (0 = forever). Releasing it finishes the current slice.
- **DIRECTION**: forward, reverse, ping-pong (alternating passes) or random.
- **QUANTIZE** delays triggers to the next 1/32 … 1 bar.
- **REGION** start/end limit which slices are reachable. Note offsets wrap inside the region.
- **TAIL**: *Ring* lets each slice play out. *Cut* gates it after **DURATION** × its time slot.
- **OVERLAP** is how long a slice keeps fading out after the next one starts.

## Outputs

| Output | |
|---|---|
| L, R | audio, ±5 V, polyphonic |
| SLICE | index of the playing slice at 1/12 V per slice, so it can feed V/OCT directly |
| TRIG | 1 ms pulse at each slice start |
| EOC | 1 ms pulse at the end of each phrase pass |

## Saving

The patch stores the slice map and per-slice settings. With **Embed audio in patch** on (the default), the cropped loop (up to 5 minutes) is saved inside the patch too, so it opens on another machine without the original file.

## Supported files

WAV (16/24/32-bit PCM, 32-bit float, RF64), AIFF/AIFC, Wave64, FLAC, MP3 and Ogg Vorbis. Stereo and mono files play as they are. Files with more channels use the first two. Files longer than 30 minutes are rejected.

### Loop metadata in WAV files

Spools reads Acidized WAV tempo and beat count (`acid`), slice markers (`cue ` points, named by `LIST/adtl/labl`), and the sampler loop and root note (`smpl`). Exports write the same chunks, so slices and tempo survive a round trip through other tools. Per-slice settings go in an extra `spls` chunk, which other software ignores.

### REX files (unofficial)

Spools reads REX2 (`.rx2`) files with its own decoder, built from public reverse-engineering work. It is not made or endorsed by Reason Studios, so please report problems here and not to them. REX1 `.rex`/`.rcy` files are not supported yet. To use one, re-save it as `.rx2`, or export it as WAV with slice markers.
