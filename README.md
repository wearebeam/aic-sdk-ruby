# aicoustics

Ruby FFI bindings for the [ai-coustics SDK](https://github.com/ai-coustics/aic-sdk-c)
(`v0.20.0`) — server-side **speech enhancement**, **voice activity detection**, and the
**Tyto** audio-quality analyzer. Built so Magic Notes can move audio enhancement and VAD
off the browser and onto the backend, keeping the frontend to capture + transport.

This is a thin, faithful wrapper over the vendored native library (the same core the
official `@ai-coustics/aic-sdk-wasm`, `aic-sdk-py`, and `aic-sdk-rs` packages wrap). No
compilation step — it loads the prebuilt `libaic.{so,dylib}` via `ffi` at runtime.

> Validated on `aarch64-darwin` (Ruby 3.4.7): library load, version/VAD/error paths, PCM
> conversion, and real v5 model loading. Enhancement/VAD processing requires a license key
> (see Licensing).

## Install

The proprietary `libaic` binaries and `aic.h` are **not committed** here — same as
ai-coustics' own bindings (`aic-sdk-rs`, `aic-sdk-py`, …), which download or wheel-bundle
them. The `LICENSE.AIC-SDK` §2 forbids redistributing the SDK via a public repo separate
from an application, so this repo holds the Apache-2.0 wrapper source only.

After cloning, fetch the binaries from ai-coustics' official releases (requires `gh`):

```bash
rake vendor:fetch            # downloads libaic for all 4 platforms into vendor/aic/
bundle exec rspec            # offline specs pass without a license
```

Add it to Magic Notes by path or git:

```ruby
# Gemfile
gem "aicoustics", git: "https://github.com/jjholmes927/aic-sdk-ruby.git"
# or local: gem "aicoustics", path: "../aicoustics"
```

`ffi` is already in the Magic Notes bundle, so this adds no new transitive dependency.
Build a deployable `.gem` (with binaries bundled, for private/internal use only) by
running `rake vendor:fetch` before `gem build` — never `gem push` to a public registry.

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
analyzer.buffer_interleaved!(float_buffer)
analyzer.analyze                                 # => AnalysisResult(risk_score:, noise:, ...)
```

Errors map the SDK's `AicErrorCode` to typed exceptions under `Aicoustics::Error`
(`LicenseExpiredError`, `ModelVersionUnsupportedError`, `EnhancementNotAllowedError`, …),
each carrying `#code`.

## Magic Notes integration (sketch)

The natural insertion point is `Interpret::Transcription::BatchTranscribeTurn`, right
before the Scribe call, behind a feature flag:

```ruby
audio_data = pcm_chunks.join

if Feature.enabled?(:interpret_server_side_enhancement, account)
  audio_data = Aicoustics.enhance_pcm(
    audio_data,
    model: AicousticsModel.path,                       # vendored/cached v5 .aicmodel
    license_key: Rails.application.credentials.ai_acoustics_sdk_key,
    sample_rate: Interpret::Conversation::SAMPLE_RATE, # 16_000
    enhancement_level: 1.0
  ).pcm
end

Interpret::Transcription::ScribeBatchTranscribe.call(pcm_data: audio_data)
```

The same pass can return `speech_flags` to drive a server-side silence gate (the audio is
mono 16 kHz, exactly what Scribe consumes as `audio/L16` / `pcm_s16le_16`).

## Models

- Download from `https://artifacts.ai-coustics.io/`. SDK `0.20.0` requires **model
  version 5** (`Aicoustics.compatible_model_version # => 5`).
- The frontend currently loads a **v4** `quail-s-16khz` model; for the backend use a **v5**
  model, e.g. `quail-vf-2-1-s-16khz/v5/quail_vf_2_1_s_16khz_5i8jb8of_v12.aicmodel`.
- Optimal frame size is **model-dependent** (this model is 240 samples / 15 ms at 16 kHz).
  Always read `model.optimal_num_frames(rate)` — do not hardcode.
- `rake "model:fetch[URL=...]"` downloads a model for local dev (gitignored).

## Deployment notes

- **glibc only.** The Linux builds target `*-unknown-linux-gnu`. Run on a glibc base image
  (Debian/Ubuntu slim) — **not** Alpine/musl.
- **Network egress required.** `process_*` returns `enhancement_not_allowed` if the SDK
  cannot reach ai-coustics to authorize the key and report usage. Cloud Run workers already
  have egress (they call LLMs), so this is satisfied.
- **License tier.** The existing `ai_acoustics_sdk_key` credential is provisioned for
  browser use; confirm it (or a new key) permits server-side usage before rollout.
- **Threading.** A `Processor` is single-threaded for `process_*` — create one per thread
  / GoodJob worker. The `*_context` parameter/VAD APIs are thread-safe.

## Supported platforms

Vendored under `vendor/aic/<sdk-version>/<arch>-<os>/`:

| | Linux (glibc) | macOS |
|---|---|---|
| x86_64 | `libaic.so` | `libaic.dylib` |
| aarch64 | `libaic.so` | `libaic.dylib` |

Override the resolved path with `AIC_SDK_LIB_PATH`. Bump the bundled SDK with
`rake "vendor:fetch[VERSION=0.21.0]"` (requires `gh`), then update
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

Binding source is Apache-2.0. The vendored `libaic.*` and `aic.h` are proprietary
ai-coustics SDK artifacts — do not redistribute publicly. See `LICENSE`.
