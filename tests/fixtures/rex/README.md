# REX fixtures

`mono.rx2` and `stereo.rx2` were made from `mono_src.wav` and `stereo_src.wav` (synthetic, generated for this repo) with `rex-encode` from [schwung-rex](https://github.com/charlesvestal/schwung-rex) at `c46ed64` (MIT):

```sh
rex-encode stereo_src.wav stereo.rx2 0,11025,22050,33075,44100 240
```

They test that our decoder agrees bit-for-bit with that independent encoder. No ReCycle-made files are included.
