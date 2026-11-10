# Grayscale chroma-conversion validation

Source patch: [40e65dbc7674bdcf10f7ec848713c8a148cf8db8](https://github.com/ksu-gitreaper807/FFmpeg/commit/40e65dbc7674bdcf10f7ec848713c8a148cf8db8).
The upstream proposal is the `sws-skip-unused-chroma` branch. This supporting
branch contains diagnostic code and evidence outside that proposal's diff.

The change prevents the input U/V conversion descriptor from being scheduled
for grayscale destinations. The existing no-chroma descriptor remains to
maintain slice metadata. Only `libswscale/slice.c` changes in the proposal.

## Correctness

The detailed comparisons were collected against FFmpeg commit
`7afbfcbd92d6a73686a86416c1ade94bfc670d07`. The patch was subsequently moved
to `cc583787bbecd4914fee7c2bf7f09463113822fe`, with no intervening changes to
`libswscale`. The configured x86 `make -j6 fate` suite passed there (201 tests).

For each target, baseline/patched active output bytes matched across:

- 2,912 legacy API cases using native CPU dispatch.
- The same 2,912 cases with dispatch forced to scalar.
- 112 modern frame API cases using the legacy backend, with one/three threads.

Targets: x86_64 Linux, physical ARM64 Android 13, QEMU AArch64, and QEMU
PowerPC64 big-endian. QEMU runtime probes established 64-bit little-endian
AArch64 and 64-bit big-endian PowerPC64; this goes beyond endian-specific
pixel layout tests on an x86 host.

For the 65x18 RGB0->GRAY8 probe, actual U/V conversion callback counts changed
from 18 to zero. `validate_rgb0.c` counts the actual converter callback,
not the metadata-only chroma stage that remains in the pipeline.

The non-x86 baseline and patched builds each passed these FATE targets:
`fate-sws-range-convert`, `fate-sws-unscaled-threaded`, and
`fate-sws-floatimg-cmp`.

The matrix includes RGB0/BGR0/0RGB/0BGR, packed and planar input, grayscale
and color outputs, high-bit-depth and endian-specific pixel formats, odd
widths, scaling, different range/matrix/rounding settings, negative input
strides, and two-row sliced input. Options are combined into eight variants,
not a full Cartesian product of all settings.

Raw per-target metadata and hashes are in `results/`. Native and scalar
results can differ from each other; comparisons are always baseline versus
patched within the same architecture and dispatch mode.

## Reproduce a diagnostic

Build baseline FFmpeg and save its static `libswscale.a` and `libavutil.a`.
Apply the source patch, rebuild with identical configure options, and save
the patched libraries. Compile each diagnostic against the corresponding
configured build tree (including its generated headers) and source tree:

```sh
cc -std=c17 -O2 -I/path/to/build -I/path/to/source validate_rgb0.c \
  /path/to/libswscale.a /path/to/libavutil.a -lm -pthread -o validate
./validate verify > native.bin
./validate verify c > scalar.bin
```

Run this for baseline and patched libraries. Compare the complete output
files with `cmp`, separately for native and scalar modes. Compile and run
`validate_frames.c` the same way and compare its stdout. Diagnostics require
internal headers from the matching configured source/build tree.

## Timing

Intel Core i7-12800H, x86_64 Linux, external assembly enabled, one conversion
thread pinned to CPU 2. RGB0 input, GRAY8 output, BT.709, limited output range,
bilinear, unchanged dimensions. Seven alternating baseline/patched runs;
five warmup frames and 60 timed frames per case. Conversion only: no decode,
encode, or CLI startup work.

| Size | Baseline median ms/frame | Patched median ms/frame |
|---|---:|---:|
| 1920x1080 | 7.498 | 5.707 |
| 3840x2160 | 30.813 | 24.729 |

This is approximately 24% and 20% less conversion time on that machine. The
unchanged YUV420P control varied by approximately -2% to +5%; no YUV420P
speedup is claimed. One of seven 1080p patched runs was slower than its
baseline pair; all seven 4K gray runs were faster. No performance measurements
were collected on the phone or in QEMU.

`benchmark_rgb0.py` expects executables compiled from `validate_rgb0.c` at
`work/validate-baseline-asm` and `work/validate-patched-asm`, relative to the
runner's working directory. Adjust CPU affinity for the target machine. Full
individual measurements are in `results/benchmark-x86.json`.

## Visual example and 512x320 timing

A separate [RGB0-to-GRAY8 example](pixel-comparison/README.md) includes the
input image, actual output from both builds, raw output files, diagnostic
sources and every timing sample. All 163,840 output pixels matched exactly;
the untimed probe counted 320 input U/V converter calls before the patch and
zero after it.

For this exact image, an uninstrumented conversion-only benchmark measured
median times of **0.338 ms/frame unchanged** and **0.247 ms/frame patched**:
**27.0% less time**, equivalent to **37.1% higher throughput**. This uses full
output range, unlike the earlier limited-range 1080p/4K measurements above.
Nine alternating pairs each used 500 warmup conversions and 4,800 timed
conversions per build, pinned to CPU 2 on the same i7-12800H.

The same input buffer was reused in memory. Setup, allocation and file I/O
were excluded; outputs matched after every sample. Existing power settings
were retained and CPU frequency was not fixed. Individual paired time
reductions ranged from 20.8% to 42.0%; the figure is a local conversion
measurement, not an end-to-end video speedup or a cross-platform estimate.

See the [comparison graphic](pixel-comparison/comparison.png),
[raw measurements](pixel-comparison/benchmark-results.json) and
[reproduction instructions](pixel-comparison/README.md#reproduce-on-x86-linux).

The standalone diagnostic sources in this directory are provided under
LGPL-2.1-or-later, consistent with the FFmpeg library code being exercised.
