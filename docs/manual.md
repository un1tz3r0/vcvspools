# Spools manual

Spools is an early proof of concept. See `DESIGN.md` for the planned behaviour and `TODO.md` for progress. This manual grows with each milestone.

## Quick start

1. Right-click the module and choose **Load sample…**. Pick a WAV/AIFF/FLAC/MP3/Ogg file or a REX2 loop.
2. Plain audio is sliced automatically at its hits, and its tempo is estimated (shown with a `?`). REX and Acidized files keep their own slices and tempo. Use the SETTINGS page (Slice by, Grid, Sensitivity, Grid snap) to change how it is cut, or **Slice evenly** for equal slices.
3. Patch a gate or trigger into **GATE**. C4 (0 V) on **V/OCT** plays the start slice, and each semitone up or down plays the next or previous slice.
4. Patch a clock into **CLOCK** and set its resolution in the menu. Without one, Spools uses the **BPM** knob, or the loop's own tempo when the knob is at 0.

## Panel

- **Display**: click the tabs along its top to switch pages.
  - **LOOP**: the loop, coloured by slice, with a playhead per voice. The header shows the file, the current BPM (a `?` means it was guessed), the slice count and the length.
  - **SETTINGS**: every global option: mode, direction, tail, loop count, envelope retrigger, slicing, clock resolution, bar length, bars per cycle, interpolation quality and audio embedding. Click a row to pick a value. The same options are also in the right-click menu, grouped as Playback, Slicing, Transport and Engine.
- **Tone strip** (the shaded band under the display): FILTER type (off, low-pass 12 dB, band-pass, high-pass 12 dB, notch, low-pass 24 dB), CUTOFF and RES, each with a CV-amount knob and a CV jack. Cutoff CV is 1 V/oct at 100%; resonance CV is 10 V = 100%. F.ENV sets how far the filter envelope moves the cutoff (±5 octaves). Then come the FILTER ENV and AMP ENV attack, decay, sustain and release knobs (0 to 10 s, exponential).
- **Main row**: SLICE (start slice), LENGTH, trim IN/OUT, PITCH, LEVEL, XFADE (overlap), DUR (gate length in Cut mode), QUANT (launch quantize), BPM. Each knob with a CV jack has a small CV-amount knob above the jack.
- **FROM** and **TO** (region) sit under DUR and BPM.
- **Bottom**: V/OCT, GATE, CLOCK, RUN, CONT and BAR inputs; outputs L, R, SLICE, TRIG, EOC, AENV, FENV.

## Playing slices

- Each trigger plays **LENGTH** slices. Each slice starts on its own beat position, scaled to the current tempo. Pitch is independent of tempo.
- A gate held past the first slice keeps repeating the phrase, up to **LOOP COUNT** passes (0 = forever). Releasing it finishes the current slice.
- **DIRECTION**: forward, reverse, ping-pong (alternating passes) or random.
- **QUANTIZE** delays triggers to the next 1/32 … 1 bar.
- **REGION** start/end limit which slices are reachable. Note offsets wrap inside the region.
- **TAIL**: *Ring* lets each slice play out. *Cut* gates it after **DURATION** × its time slot.
- **OVERLAP** is how long a slice keeps fading out after the next one starts.
- **Per-slice settings** add to the knobs: a slice's own pitch, level, pan, trims, cutoff, resonance and crossfade are offsets on top of the knob and CV values. The sums are limited to a usable range (for example ±48 semitones, +12 dB). Pitch, level, cutoff and resonance follow knob and CV changes while a slice plays, with a 5 ms glide. The other settings are read when each slice starts.
- **Envelopes** restart on each slice by default, so every hit gets its own attack. Set **Retrigger** to *Each note* on the SETTINGS page runs them once per triggered phrase instead. The amp envelope's release starts when the note ends (gate released, or the Cut time is up). A release shorter than OVERLAP uses the overlap fade instead. The default (no attack, full sustain, no release) leaves the sound unchanged.

## Transport

Set **Mode** to *Loop* on the SETTINGS page plays the region continuously on the clock instead of on gates; the display header shows LOOP. It starts at the SLICE knob's slice and keeps each slice's own timing, so it stays in step with the bar however the clock moves. **Direction** and **Loop count** work per pass through the region (loop count 0 = forever). GATE is ignored. V/OCT shifts which slice sounds in each slot by one slice per semitone, for beat-repeat and re-chop effects; each poly V/OCT channel runs its own copy.

RUN, CONT and BAR drive loop mode. RUN is a gate: high runs the loop, and its rising edge restarts it at the start slice. Unpatched means always running. CONT restarts at the start slice on the next clock pulse (at once with no clock). BAR realigns the bar count without moving the playback position. The SETTINGS page (or right-click → **Transport**) sets the **Bar length** (from the loop's meter by default) and **Bars per cycle**, which restarts the loop automatically every N bars (∞ = only on CONT).

**Direction**, **Tail** (Ring/Cut) and **Loop count** are on the SETTINGS page (or right-click → **Playback**).

## Crop and tempo

Right-click → **Crop** sets which part of the file plays. Type a start and a length and press Enter. A bare number uses the chosen **Units**; a suffix overrides it: `1.5 s`, `250 ms`, `4 b` (beats), `2 bars`, `1000 smp`. Leave the length empty to play to the end, or pick **Whole file**. Cropping plain audio slices it again; REX and Acidized files keep the slices inside the crop.

Right-click → **Loop tempo** sets the loop's own tempo, which fixes where each slice sits in beats. Type a BPM, **Halve** or **Double** it, or click **Tap tempo** on each beat (the menu stays open). A detected tempo shows `?` on the display until you set one. The **BPM** knob is separate: it sets the playback tempo when no clock is patched.

## Auto-slice

The SETTINGS page (or right-click → **Slicing**). Every change re-slices at once, and the setting is saved with the patch. Re-slicing resets per-slice settings.

- **Mode**: *Onsets* cuts at each detected hit. *Grid* cuts at every grid step. *Hybrid* uses the grid plus any strong hit that falls between grid lines.
- **Grid**: 1/4 to 1/32 notes, or 1/8 and 1/16 triplets.
- **Sensitivity**: higher finds quieter hits.
- **Grid snap**: in Onsets mode, moves each hit toward its nearest grid line, from 0% (where it was found) to 100% (on the grid).
- **Re-slice now** applies the settings to a REX or Acidized file too, replacing its own slices.

## Outputs

| Output | |
|---|---|
| L, R | audio, ±5 V, polyphonic |
| SLICE | index of the playing slice at 1/12 V per slice, so it can feed V/OCT directly |
| TRIG | 1 ms pulse at each slice start |
| AENV, FENV | amp and filter envelopes, 0–10 V |
| EOC | 1 ms pulse at the end of each phrase pass |

## Saving

The patch stores the slice map and per-slice settings. With **Embed audio in patch** on (the default), the cropped loop (up to 5 minutes) is saved inside the patch too, so it opens on another machine without the original file.

## Supported files

WAV (16/24/32-bit PCM, 32-bit float, RF64), AIFF/AIFC, Wave64, FLAC, MP3 and Ogg Vorbis. Stereo and mono files play as they are. Files with more channels use the first two. Files longer than 30 minutes are rejected.

### Loop metadata in WAV files

Spools reads Acidized WAV tempo and beat count (`acid`), slice markers (`cue ` points, named by `LIST/adtl/labl`), and the sampler loop and root note (`smpl`). Exports write the same chunks, so slices and tempo survive a round trip through other tools. Per-slice settings go in an extra `spls` chunk, which other software ignores.

### REX files (unofficial)

Spools reads REX2 (`.rx2`) files with its own decoder, built from public reverse-engineering work. It is not made or endorsed by Reason Studios, so please report problems here and not to them. REX1 `.rex`/`.rcy` files are not supported yet. To use one, re-save it as `.rx2`, or export it as WAV with slice markers.
