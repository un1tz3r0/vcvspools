# Vendored libraries

| Path | Upstream | Version | License |
|---|---|---|---|
| `doctest/doctest.h` | doctest/doctest | v2.4.11 (`ae7a135`) | MIT (tests only) |
| `dr_libs/dr_wav.h`, `dr_flac.h`, `dr_mp3.h` | mackron/dr_libs | `e381981` | public domain or MIT-0 |
| `stb/stb_vorbis.c` | nothings/stb | v1.25 (`6b5799b`) | public domain or MIT |

All decoders are compiled only in `core/decode.cpp`, with internal linkage. Update a library by replacing its file with an upstream file at a pinned commit and recording the commit here.

## Derived code

`core/rex.cpp` is our own C++ implementation of the REX2 container and DWOP codec. It follows the reverse-engineered description and reference decoder in [schwung-rex](https://github.com/charlesvestal/schwung-rex) (MIT, © 2026 Charles Vestal). REX and ReCycle are trademarks of Reason Studios AB. Spools is not affiliated with or endorsed by them.
