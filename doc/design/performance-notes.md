# Performance notes

Measurements kept for later decisions. Each says what was measured, on what, and what it
suggests. Numbers from a different machine or build are not comparable.

## Reduced levels: JPEG scaled decode vs full decode and halving (2026-11-07)

### Why it was measured

A JPEG and the JPEG XL repacked from it without loss look the same at 1:1, but not at fit.
The JPEG XL is up to 3 levels brighter and slightly less saturated in detailed mid-shadows,
and its histogram differs. Measured from the screen under Xvfb, JPEG XL minus JPEG:

| Luma | At fit | At 1:1 |
|---|---|---|
| 0-32 | +2.4 | +0.05 |
| 64-96 | +3.0, saturation -0.7 | +0.03 |
| 96-128 | +1.5 | ±0.04 |
| 128-255 | ±0.1 | ±0.1 |

The cause is how each format reaches the level shown at fit:

* JPEG: libjpeg's scaled decode (1/2, 1/4, 1/8) uses only the low frequencies of each block.
  This is roughly a block average of gamma-encoded values, and no full-size image exists.
* Everything else (JPEG XL, PNG, WebP, AVIF, and TIFF below its stored levels): the full image
  is halved repeatedly with 1-3-3-1 weights in linear light (`halve` in `image_ops.cpp`).

Averaging in linear light is the physically right way to shrink: fine dark detail such as
leaves against shadow keeps its share of light. Averaging gamma-encoded values darkens it and
keeps more of its colour. The JPEG path matches what most viewers show; the other path is
more correct. The histogram is taken from a reduced level, so it differs the same way.

### Setup

* Intel Core i5-8350U (4 cores, 8 threads, 2017 laptop), 24 GB RAM, Linux, power profile
  "balanced", so clocks were likely below the 3.6 GHz maximum. The ratios matter more than the
  absolute times.
* RelWithDebInfo build. libjpeg-turbo 3.2.1, libjxl 0.12.
* A 5776x4336 (25 MP) camera JPEG, 13 MB, 4:2:0, and the JPEG XL repacked from it, 10.7 MB.
* Median of 7 runs, the file already in memory.

### Results

| Way to the 1/4 level used at fit | Time |
|---|---|
| Today: 1/8 preview, then the 1/4 level, both scaled by libjpeg | 246 ms (106 + 140) |
| Full decode | 213 ms |
| Halving in linear light, to 1/2 then to 1/4 | 237 ms (190 + 47) |
| Full decode + halving | 450 ms |
| Same, with the 1/8 preview first | 556 ms |
| JPEG XL open + full decode (libjxl on all threads) | 175 ms |

### Observations

* Entropy decoding dominates a JPEG decode: the full decode costs only about 50% more than the
  1/4 scaled one. Decoding at full size is not the expensive part of making JPEG consistent.
* The halving is as slow as the decode. It runs on one thread, converting every sample through
  a table to float and back. The first halving reads 100 MB and writes 25 MB, which takes this
  machine's memory a few milliseconds, so at 190 ms it is limited by computation, not by memory
  bandwidth. It should parallelize well across rows, and narrower arithmetic (16-bit linear
  values) or SIMD would help further. Estimated, not measured: around 50 ms on 8 threads.
* Every non-JPEG format pays for the halving after its decode, so speeding it up helps them all.
* Making JPEG consistent with the others would cost about +310 ms per open with today's
  halving, or perhaps +100-150 ms with a parallel one, plus about 100 MB for the full-size image
  while levels are built.
* Averaging in gamma space for 8-bit images instead would remove the difference at no cost,
  at the price of the less correct, though familiar, result.

### Making the halving faster

In order of effort:

1. Threads. Rows of the target are independent once their four source rows are known, so
   bands of rows can run on the worker queue. Up to 4x on this machine.
2. Integer arithmetic the compiler vectorizes on its own. Decode through a 256-entry table to
   16-bit linear values, sum the 1-3-3-1 weights in 32-bit integers (64 x 65535 fits), and
   encode back through a table only for the output pixels, a quarter of the input. The row
   sums are plain vector additions that GCC, Clang and MSVC vectorize at -O2 without
   intrinsics. Today's loop goes through floats and a table lookup per sample, which does not
   vectorize.
3. Explicit SIMD, only if the above is not enough. x86-64 guarantees SSE2 only, AVX2 needs a
   check at run time, and ARM machines (Apple, Snapdragon laptops) need NEON. Highway
   (Apache 2.0 or BSD-3) covers all of them with dispatch at run time, and libjxl already
   depends on it. xsimd (BSD-3) is an alternative. Hand-written intrinsics per platform are
   the last resort.

libjpeg-turbo and libjxl already use SIMD internally, so the decoders need nothing from us.

### Measuring it properly

* Google Benchmark (Apache 2.0) as an optional `iv_benchmarks` target, built only when
  `find_package(benchmark)` finds it, like the optional formats. Worth adding with the first
  change whose before and after numbers decide something, which is the faster halving. The
  numbers above came from a throwaway program using `std::chrono`.
* Tests make their images in memory, in the test or the fixture's constructor, with flat
  colours and gradients at 64x64, so that a wrong turn or crop shows as a large difference.
  Nothing is generated before the tests and no image files are in the repository.
* Benchmarks need realistic content instead, as detail and noise drive the entropy-coded size,
  and entropy decoding was most of the JPEG time. Start with our own generator of about 30
  lines (value noise over a few octaves plus per-pixel grain, fixed seed), and take FastNoise
  Lite (MIT, one header) only if that looks too unlike photos. Generate a 24 MP image once per
  process, lazily into a static, not per iteration. Accept an optional path to a real photo,
  as synthetic JPEGs compress somewhat differently.

### Where Magic Kernel Sharp runs

* On the GPU: the optional sharp resampling of the view (M), as two shader passes over the
  level chosen for the zoom. It does not build levels.
* On the CPU: `resize`, used for thumbnails and meant for exports. It is not used for levels.
* Levels are made by the 1-3-3-1 halving on the CPU, which is cheaper than MKS and never
  overshoots.

### Why levels are not GPU mipmaps

A mipmap needs the full-size texture on the GPU, which a gigapixel TIFF cannot have. Tiled
levels serve a photo and a gigapixel image the same way, with only the visible tiles uploaded.
The CPU halving also filters better than a hardware 2x2 box filter, and JPEG gets its levels
straight from the scaled decode.

### JPEG XL progressive decoding

VarDCT JPEG XL keeps the DC of every 8x8 block as a 1/8 image (a repacked JPEG keeps exactly
the JPEG's DC there), and an encoder may add further DC layers at 1/64 and 1/512. libjxl's API
hands out only full-size buffers, though: a progressive flush returns the 1/8 data upsampled to
full size. That suits an early blurry preview on a slow load, but not a cheap reduced level the
way libjpeg's scaled decode is. The viewer does not use progressive decoding yet.

### Open decision

Make the levels consistent across formats: speed up the halving first, measure again, then
choose between a full decode for JPEG (correct, slower to open) and halving in gamma space for
8-bit images (familiar, free). The halving is now faster; see below.

## Faster halving (2026-11-07)

Done as planned: steps 1 and 2 above, no explicit SIMD.

* 8-bit images go through integers. Each sample becomes 16-bit linear light through a 256-entry
  table, the 1-3-3-1 weights are summed in 32-bit integers, and the average is encoded through a
  table with an entry for each of the 65536 linear values. The other formats keep the float path.
* `halveRows` splits the target rows into bands on up to 8 threads, from 512K output pixels up.
  Bands only read the source, so the result is byte for byte the same as one band at a time,
  which a test checks for every format.
* The finer encoding table makes 8-bit results more accurate, not only faster. Against the same
  halving computed in doubles, on 2.7 million samples of a noisy 25 MP image: the float path
  with its 4096-step table was one level off in 52,490 samples, the integer path in 3,625. 1.6%
  of samples differ from the old output, all by one level. 16-bit and float output is unchanged.

Same machine and image size as above, the first halving of a 25 MP image, median of 7. The
machine warms up during long runs, so the same build measured 23 to 36 ms for 8-bit in one
afternoon; compare within one run.

| Format | Before | Integers, one core | Integers and threads |
|---|---|---|---|
| 8-bit | 192 ms | 99 ms | 23-36 ms |
| 16-bit | 121 ms | 125 ms | 32-51 ms |
| float | 135 ms | | 68-113 ms |

Float now reads 400 MB and writes 100 MB per halving, so it is bound by memory bandwidth.

`iv_benchmarks` (Google Benchmark, built only when it is installed) measures these on a
synthetic image of smooth noise over four scales plus grain, generated once per process.
`IV_BENCHMARK_IMAGE=path` adds a real file: its full decode, the full decode with two halvings,
and its 1/4 level as the viewer reads it now. A source keeps what it decodes, so each iteration
opens the file again.

### JPEG levels, measured again

A synthetic 25 MP JPEG, 4:2:0, quality 92, 10 MB:

| Way to the 1/4 level | Time |
|---|---|
| Today: libjpeg's scaled decode (open + 1/4) | 149 ms |
| Full decode | 230 ms |
| Full decode + halving twice | 258 ms |

Halving was 237 of the 450 ms that the consistent path cost; it is now about 30-45 ms. A full
decode for JPEG would cost about +110 ms against the 1/4 decode alone, plus about 100 MB while
levels are made. Today the viewer also decodes a 1/8 preview first (about 106 ms on the camera
JPEG above) so that something shows early. Replacing both with the full decode takes about as
long in total but shows nothing until it is done; keeping the preview and then decoding fully
costs about +110 ms before the sharp image at fit. libjpeg decodes on one thread, which is now
most of the time.
