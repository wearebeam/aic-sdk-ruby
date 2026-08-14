# aicoustics

Native C extension bindings for the [ai-coustics SDK](https://github.com/ai-coustics/aic-sdk-c)
(`v0.23.0`) — **speech enhancement**, **voice activity detection**, and the **Tyto 1.1**
audio-quality analyzer, for running ai-coustics audio processing server-side from Ruby.

The SDK is mono-only as of 0.21: every audio API takes a single-channel block of
`block_size` float32 samples. Downmix (or run one handle per channel) upstream.

This is a thin, faithful wrapper over the vendored native library (the same core the
official `@ai-coustics/aic-sdk-wasm`, `aic-sdk-py`, and `aic-sdk-rs` packages wrap). It is a
compiled C extension that links the prebuilt `libaic.{so,dylib}` at build time and loads it
via an rpath at runtime (no FFI, no runtime gem dependencies). The heavy `process` calls
release the GVL so audio crunching runs in parallel.

> Validated on `aarch64-darwin` (Ruby 3.4.7): library load, version/error paths, PCM
> conversion, and licensed enhancement/VAD/Tyto runs against real v7 models. Processing
> requires a license key (see Licensing).

## Install

The proprietary `libaic` binaries and `aic.h` are **not committed** here — same as
ai-coustics' own bindings (`aic-sdk-rs`, `aic-sdk-py`, …), which download or wheel-bundle
them. The `LICENSE.AIC-SDK` §2 forbids redistributing the SDK via a public repo separate
from an application, so this repo holds the Apache-2.0 wrapper source only.

After cloning, fetch the binaries from ai-coustics' official releases (a plain HTTPS
download — no auth or `gh` needed), then compile the extension (needs a C toolchain —
`cc`/`clang` and `make`):

```bash
rake vendor:fetch            # downloads + checksum-verifies libaic + aic.h for all 4 platforms
bundle exec rake compile     # builds the C extension against the vendored libaic
bundle exec rspec            # offline specs pass without a license (rake spec compiles first)
```

`rake vendor:fetch` is optional for a from-source checkout: if `libaic` isn't already
vendored, `extconf.rb` downloads and checksum-verifies it for the current platform on
`bundle install`. There are **no runtime gem dependencies** — the extension links `libaic`
directly.

For unreleased work you can point an app straight at the source:

```ruby
# Gemfile — development only
gem "aicoustics", git: "https://github.com/wearebeam/aic-sdk-ruby.git"
# or local: gem "aicoustics", path: "../aicoustics"
```

## Using the published gem (GitHub Packages)

Released versions are published to the org's **private** GitHub Packages RubyGems registry.
The published gem contains **only** the Apache-2.0 wrapper and the C extension sources —
never the proprietary `libaic`. That binary is fetched per platform at *your* `bundle
install` time by `extconf.rb` (a plain HTTPS download from the public ai-coustics release,
checksum-verified), so the build host needs a **C toolchain (`cc`/`clang`, `make`) and
outbound network**.

Point the app at the registry:

```ruby
# Gemfile
source "https://rubygems.pkg.github.com/wearebeam" do
  gem "aicoustics"
end
```

Authenticate Bundler with a GitHub token that has `read:packages` (a PAT locally, or
`secrets.GITHUB_TOKEN` in CI):

```bash
# local — writes to ~/.bundle/config
bundle config set --global https://rubygems.pkg.github.com/wearebeam USERNAME:TOKEN

# CI — env var, no file
export BUNDLE_RUBYGEMS__PKG__GITHUB__COM=USERNAME:TOKEN
```

Then `bundle install` as usual.

### Publishing a release

Bump `Aicoustics::VERSION`, then run the **Publish** workflow (Actions tab → Run workflow).
It builds the gem and pushes it with the built-in `GITHUB_TOKEN`; a guard fails the run if
that version is already published.

## Quick start

```ruby
require "aicoustics"

Aicoustics.sdk_version            # => "0.23.0"
Aicoustics.compatible_model_version # => 7

result = Aicoustics.enhance_pcm(
  pcm_s16le,                      # raw 16-bit little-endian mono PCM (a turn's audio)
  model: "/path/to/enhancement_model.aicmodel",
  license_key: ENV.fetch("AIC_SDK_LICENSE"),
  sample_rate: 16_000
)

result.pcm                 # enhanced 16-bit PCM, delay-compensated, same length as input
result.audio_delay_samples # processor algorithmic latency, in samples
```

`enhance_pcm` handles slicing into the model's optimal block size, Int16↔Float32
conversion, flushing the processor tail, and trimming the algorithmic delay so the output
is time-aligned to the input.

## Lower-level API

```ruby
model     = Aicoustics::Model.from_file(path)   # also .from_buffer(bytes)
model.id                                        # e.g. "tyto-1.1-l-16khz"
model.optimal_sample_rate                       # 16000
model.optimal_block_size(16_000)                # 240 (model-dependent — never hardcode)

processor = Aicoustics::Processor.create(model, license_key)
processor.configure(sample_rate: 16_000)        # block_size: defaults to the model optimum

processor.process!(floats.pack("f*"))           # mono float32 binary String, enhanced in place
processor.context.enhancement_level = 1.0       # 0.0..1.0 (model-dependent meaning)
processor.context.bypass = false
processor.context.audio_delay                   # samples
processor.context.reset

# Voice activity detection (needs a dedicated VAD model, e.g. Quail VAD)
vad = Aicoustics::Vad.create(vad_model, license_key)
vad.configure(sample_rate: 16_000)
vad.process!(floats.pack("f*"))                 # feed the ORIGINAL audio, not enhanced output
vad.context.sensitivity = 0.5                   # 0.0..1.0
vad.context.speech_detected?
vad.context.raw_vad_probability                 # model output before hold/threshold post-processing
vad.context.prediction_delay                    # samples the prediction lags its input

# Tyto audio-quality analyzer (needs a Tyto analysis model, not an enhancement model)
analyzer = Aicoustics::Analyzer.create(tyto_model, license_key)
analyzer.configure(sample_rate: 16_000)
analyzer.buffer!(floats.pack("f*"))             # mono float32 binary String
analyzer.analyze                                # => AnalysisResult(risk_score:, noise:, codec_degradation:, ...)
```

Errors map the SDK's `AicErrorCode` to typed exceptions under `Aicoustics::Error`
(`LicenseExpiredError`, `ModelVersionUnsupportedError`, `ProcessingNotAllowedError`, …),
each carrying `#code`.

## Models

- Download from `https://artifacts.ai-coustics.io/`. SDK `0.23.0` requires **model
  version 7** (`Aicoustics.compatible_model_version # => 7`) — models built for older
  SDKs (v5 and below) are rejected at load, so re-download every model when bumping.
- Optimal block size is **model-dependent**. Always read `model.optimal_block_size(rate)`
  — do not hardcode.
- `URL=... rake model:fetch` downloads a model for local dev (gitignored).

## Deployment notes

- **glibc only.** The Linux builds target `*-unknown-linux-gnu`. Run on a glibc base image
  (Debian/Ubuntu slim) — **not** Alpine/musl.
- **Network egress required.** `process!` returns `processing_not_allowed` if the SDK
  cannot reach ai-coustics to authorize the key and report usage — allow outbound network
  in your runtime.
- **License tier.** Confirm your license key permits server-side usage before rollout.
- **Threading.** A `Processor`/`Vad` is single-threaded for `process!` — create one per
  thread / GoodJob worker. The `*_context` parameter APIs are thread-safe.

## Supported platforms

Vendored under `vendor/aic/<sdk-version>/<arch>-<os>/`:

| | Linux (glibc) | macOS |
|---|---|---|
| x86_64 | `libaic.so` | `libaic.dylib` |
| aarch64 | `libaic.so` | `libaic.dylib` |

The extension links the lib for the current platform via an rpath baked at build time
(`@loader_path`/`$ORIGIN` relative to the vendored dir, so the gem is relocatable). To bump
the bundled SDK: update `Aicoustics::SDK_VERSION`, regenerate the pinned checksums with
`VERSION=x.y.z rake vendor:checksums` (paste them into `ext/aicoustics/sdk_fetcher.rb`),
then `rake vendor:fetch` and recompile.

## Testing

```bash
bundle exec rspec
```

License/model-gated specs skip unless these are set:

- `AIC_SDK_MODEL` — path to an enhancement `.aicmodel` (model-load specs need no license)
- `AIC_SDK_LICENSE` — license key (enables processing/VAD specs)
- `AIC_SDK_ANALYZER_MODEL` — path to a Tyto analysis model (analyzer specs)
- `AIC_SDK_VAD_MODEL` — path to a dedicated VAD model (VAD specs)

## Licensing

Wrapper source is Apache-2.0 (`LICENSE`, see also `NOTICE`). The `libaic.*` binaries and
`aic.h` are proprietary ai-coustics SDK artifacts, fetched via `rake vendor:fetch` and
governed by `LICENSE.AIC-SDK` — do not commit or redistribute them publicly.
