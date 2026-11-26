# Spools manual

Spools is not playable yet. See `DESIGN.md` for the planned behaviour and `TODO.md` for progress. This manual grows with each milestone.

## Supported files

WAV (16/24/32-bit PCM, 32-bit float, RF64), AIFF/AIFC, Wave64, FLAC, MP3 and Ogg Vorbis. Stereo and mono files play as they are. Files with more channels use the first two. Files longer than 30 minutes are rejected.
