# aicoustics

Native C extension bindings for the [ai-coustics SDK](https://github.com/ai-coustics/aic-sdk-c)
(`v0.20.0`) — **speech enhancement**, **voice activity detection**, and the **Tyto**
audio-quality analyzer, for running ai-coustics audio processing server-side from Ruby.

This is a thin, faithful wrapper over the vendored native library (the same core the
official `@ai-coustics/aic-sdk-wasm`, `aic-sdk-py`, and `aic-sdk-rs` packages wrap). It is a
compiled C extension that links the prebuilt `libaic.{so,dylib}` at build time and loads it
via an rpath at runtime (no FFI, no runtime gem dependencies). The heavy `process` calls
release the GVL so audio crunching runs in parallel.

> Validated on `aarch64-darwin` (Ruby 3.4.7): library load, version/VAD/error paths, PCM
> conversion, and real v5 model loading. Enhancement/VAD processing requires a license key
> (see Licensing).

## Install

The proprietary `libaic` binaries and `aic.h` are **not committed** here — same as
ai-coustics' own bindings (`aic-sdk-rs`, `aic-sdk-py`, …), which download or wheel-bundle
them. The `LICENSE.AIC-SDK` §2 forbids redistributing the SDK via a public repo separate
from an application, so this repo holds the Apache-2.0 wrapper source only.

After cloning, fetch the binaries from ai-coustics' official releases (requires `gh`), then
compile the extension (needs a C toolchain — `cc`/`clang` and `make`):

```bash
rake vendor:fetch            # downloads libaic + aic.h for all 4 platforms into vendor/aic/
bundle exec rake compile     # builds the C extension against the vendored libaic
bundle exec rspec            # offline specs pass without a license (rake spec compiles first)
```

Add it to your app by git or path:

```ruby
# Gemfile
gem "aicoustics", git: "https://github.com/wearebeam/aic-sdk-ruby.git"
# or local: gem "aicoustics", path: "../aicoustics"
```

There are **no runtime gem dependencies** — the extension links `libaic` directly. Building
from git/path compiles the extension on `bundle install`. Build a deployable `.gem` (with
binaries bundled, for private/internal use only) by running `rake vendor:fetch` before
`gem build` — never `gem push` to a public registry. For zero-toolchain deploys, build
precompiled per-platform gems with `rake-compiler-dock`.

## Quick start

```ruby
require "aicoustics"

Aicoustics.sdk_version            # => "0.20.0"
Aicoustics.compatible_model_version # => 5

result = Aicoustics.enhance_pcm(
  pcm_s16le,                      # raw 16-bit little-endian mono PCM (a turn's audio)
  model: "/path/to/quail_vf_2_1_s_16khz_5i8jb8of_v12.aicmodel",
  license_key: ENV.fetch("AIC_SDK_LICENSE"),
  sample_rate: 16_000,
  vad: true
)

result.pcm                  # enhanced 16-bit PCM, delay-compensated, same length as input
result.speech_flags         # per-frame [true,false,...] VAD decisions (nil unless vad:)
result.output_delay_samples # processor algorithmic latency, in samples
```

`enhance_pcm` handles framing into the model's optimal block size, Int16↔Float32
conversion, flushing the processor tail, and trimming the algorithmic delay so the output
is time-aligned to the input.

## Lower-level API

```ruby
model     = Aicoustics::Model.from_file(path)   # also .from_buffer(bytes)
model.id                                        # "quail-vf-2.1-s-16khz-...-v12"
model.optimal_sample_rate                       # 16000
model.optimal_num_frames(16_000)                # 240 (model-dependent — never hardcode)

processor = Aicoustics::Processor.create(model, license_key)
processor.configure(sample_rate: 16_000, num_channels: 1)

processor.context.enhancement_level = 1.0       # 0.0..1.0 (model-dependent meaning)
processor.context.bypass = false
processor.context.output_delay                  # samples
processor.context.reset

vad = processor.vad
vad.sensitivity = 6.0                            # energy-based VAD: ~1.0..15.0
vad.speech_detected?                             # call after processing a frame

# Tyto audio-quality analyzer (needs a Tyto analysis model, not an enhancement model)
analyzer = Aicoustics::Analyzer.create(tyto_model, license_key)
analyzer.configure(sample_rate: 16_000, num_channels: 1)
analyzer.buffer_interleaved!(floats.pack("f*"))  # interleaved float32 binary String
analyzer.analyze                                 # => AnalysisResult(risk_score:, noise:, ...)
```

Errors map the SDK's `AicErrorCode` to typed exceptions under `Aicoustics::Error`
(`LicenseExpiredError`, `ModelVersionUnsupportedError`, `EnhancementNotAllowedError`, …),
each carrying `#code`.

## Models

- Download from `https://artifacts.ai-coustics.io/`. SDK `0.20.0` requires **model
  version 5** (`Aicoustics.compatible_model_version # => 5`).
- Use a **v5** model, e.g. `quail-vf-2-1-s-16khz/v5/quail_vf_2_1_s_16khz_5i8jb8of_v12.aicmodel`
  (validated). Models below the SDK's compatible version are rejected at load.
- Optimal frame size is **model-dependent** (this model is 240 samples / 15 ms at 16 kHz).
  Always read `model.optimal_num_frames(rate)` — do not hardcode.
- `URL=... rake model:fetch` downloads a model for local dev (gitignored).

## Deployment notes

- **glibc only.** The Linux builds target `*-unknown-linux-gnu`. Run on a glibc base image
  (Debian/Ubuntu slim) — **not** Alpine/musl.
- **Network egress required.** `process_*` returns `enhancement_not_allowed` if the SDK
  cannot reach ai-coustics to authorize the key and report usage — allow outbound network
  in your runtime.
- **License tier.** Confirm your license key permits server-side usage before rollout.
- **Threading.** A `Processor` is single-threaded for `process_*` — create one per thread
  / GoodJob worker. The `*_context` parameter/VAD APIs are thread-safe.

## Supported platforms

Vendored under `vendor/aic/<sdk-version>/<arch>-<os>/`:

| | Linux (glibc) | macOS |
|---|---|---|
| x86_64 | `libaic.so` | `libaic.dylib` |
| aarch64 | `libaic.so` | `libaic.dylib` |

The extension links the lib for the current platform via an rpath baked at build time
(`@loader_path`/`$ORIGIN` relative to the vendored dir, so the gem is relocatable). Bump the
bundled SDK with `VERSION=0.21.0 rake vendor:fetch` (requires `gh`), recompile, then update
`Aicoustics::SDK_VERSION`.

## Testing

```bash
bundle exec rspec
```

License/model-gated specs skip unless these are set:

- `AIC_SDK_MODEL` — path to an enhancement `.aicmodel` (model-load specs need no license)
- `AIC_SDK_LICENSE` — license key (enables processing/VAD specs)
- `AIC_SDK_ANALYZER_MODEL` — path to a Tyto analysis model (analyzer specs)

## Licensing

Wrapper source is Apache-2.0 (`LICENSE`, see also `NOTICE`). The `libaic.*` binaries and
`aic.h` are proprietary ai-coustics SDK artifacts, fetched via `rake vendor:fetch` and
governed by `LICENSE.AIC-SDK` — do not commit or redistribute them publicly.
