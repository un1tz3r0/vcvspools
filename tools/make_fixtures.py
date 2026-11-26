#!/usr/bin/env python3
"""Generate the small audio fixtures in tests/fixtures/. Output is committed, so CI needs no Python.

Compressed formats (FLAC, MP3, Ogg, AIFF) are made from the reference WAV with ffmpeg when it is on PATH.
"""
import shutil
import struct
import subprocess
from pathlib import Path

import numpy as np

OUT = Path(__file__).resolve().parent.parent / "tests" / "fixtures"
SR, SECONDS = 44100, 0.25


def tone(sr=SR, seconds=SECONDS):
    t = np.arange(int(sr * seconds)) / sr
    return np.stack([0.5 * np.sin(2 * np.pi * 440 * t), 0.25 * np.sin(2 * np.pi * 660 * t)])


def riff_wav(path, x, sr, fmt, extra_chunks=b""):
    """x: (channels, frames) float in [-1, 1]. fmt: 'pcm16' | 'pcm24' | 'f32'."""
    ch, n = x.shape
    inter = x.T.reshape(-1)
    if fmt == "f32":
        tag, bits, data = 3, 32, inter.astype("<f4").tobytes()
    elif fmt == "pcm16":
        tag, bits, data = 1, 16, np.round(inter * 32767).astype("<i2").tobytes()
    else:
        tag, bits = 1, 24
        q = np.round(inter * 8388607).astype("<i4")
        data = q.view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    align = ch * bits // 8
    fmt_chunk = struct.pack("<HHIIHH", tag, ch, sr, sr * align, align, bits)
    body = b"WAVE" + chunk(b"fmt ", fmt_chunk) + extra_chunks + chunk(b"data", data)
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def chunk(tag, payload):
    return tag + struct.pack("<I", len(payload)) + payload + (b"\0" if len(payload) % 2 else b"")


def acid_chunks(bpm, beats, cues, loop):
    acid = struct.pack("<IHHfIHHf", 0x02, 60, 0x8000, 0.0, beats, 4, 4, bpm)
    cue = struct.pack("<I", len(cues)) + b"".join(
        struct.pack("<II4sIII", i + 1, 0, b"data", 0, 0, frame) for i, (frame, _) in enumerate(cues))
    adtl = b"adtl" + b"".join(chunk(b"labl", struct.pack("<I", i + 1) + label.encode() + b"\0")
                              for i, (_, label) in enumerate(cues))
    smpl = struct.pack("<9I", 0, 0, 22676, 60, 0, 0, 0, 1, 0) + struct.pack("<6I", 0, 0, loop[0], loop[1] - 1, 0, 0)
    return chunk(b"acid", acid) + chunk(b"cue ", cue) + chunk(b"LIST", adtl) + chunk(b"smpl", smpl)


def ffmpeg(src, dst, *args):
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(src), *args, str(dst)], check=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    x = tone()
    riff_wav(OUT / "tone_pcm16.wav", x, SR, "pcm16")
    riff_wav(OUT / "tone_pcm24.wav", x, SR, "pcm24")
    riff_wav(OUT / "tone_f32.wav", x, SR, "f32")
    riff_wav(OUT / "tone_mono_48k.wav", tone(48000)[:1], 48000, "pcm16")
    quad = np.concatenate([x, -x])
    riff_wav(OUT / "tone_quad.wav", quad, SR, "pcm16")
    # Two beats at 120 BPM: 0.5 s at 44.1 kHz. Cue ids deliberately out of order relative to position.
    beats = tone(seconds=0.5)
    riff_wav(OUT / "acid_loop.wav", beats, SR, "pcm16",
             acid_chunks(120.0, 2, [(11025, "b"), (0, "a"), (16538, "c")], (0, 22050)))
    if shutil.which("ffmpeg"):
        src = OUT / "tone_pcm16.wav"
        ffmpeg(src, OUT / "tone.aiff", "-c:a", "pcm_s16be")
        ffmpeg(src, OUT / "tone.flac")
        ffmpeg(src, OUT / "tone.mp3", "-b:a", "128k")
        ffmpeg(src, OUT / "tone.ogg", "-c:a", "libvorbis", "-q:a", "4")
    else:
        print("ffmpeg not found: kept existing AIFF/FLAC/MP3/Ogg fixtures")


if __name__ == "__main__":
    main()
