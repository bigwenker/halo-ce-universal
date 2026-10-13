# stb_truetype

A TrueType font rasterizer in one C header, by Sean Barrett (RAD Game
Tools), public domain or MIT licensed, as the user chooses (see
`LICENSE`).

Upstream: https://github.com/nothings/stb, commit
2c980bb59875b0d32144a71867fbdebb2f77cd20 (`stb_truetype.h` v1.26), copied
unchanged.

The game's text is drawn with fonts from `port/assets/fonts`, rasterized
with it at the display's resolution (`port/linux/src/text_hires.c`); it
builds into the Linux, Windows and Android platform layers.

`stb_image.h` v2.30, from the same commit, is copied unchanged. The image
spray loader (`port/linux/src/spray_image.c`) builds its PNG, JPEG and GIF
decoders, with a 4096-pixel dimension limit and bounded decoder allocations.
`spray_gif.h` preflights animation storage and drives the pinned decoder with
the two-back frame required by GIF's restore-previous disposal, into bounded
storage; the vendored header remains unchanged.
