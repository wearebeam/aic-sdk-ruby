# Examples

Two demos for hearing the ai-coustics SDK work on a WAV file. WAV/PCM reading and
`.env` loading are factored into `support.rb`, so the scripts themselves stay focused
on the SDK (`filename → PCM → SDK`).

## Setup

```bash
rake vendor:fetch                       # fetch the native libs (if you haven't)
bundle exec rake compile                # build the C extension
cp examples/.env.example examples/.env  # then edit examples/.env and add AIC_SDK_LICENSE
```

The bundled enhancement model (`spec/fixtures/model.aicmodel`) and Tyto model
(`spec/fixtures/tyto.aicmodel`) come from `URL=... rake model:fetch`; override with
`AIC_SDK_MODEL` / `AIC_SDK_ANALYZER_MODEL`.

## `enhance_wav.rb` — A/B comparison

Enhances the whole file, then plays the original (downmixed mono) and the enhanced
version back to back.

```bash
ruby examples/enhance_wav.rb path/to/audio.wav
```

Outputs are written to `examples/output/` so you can re-listen. Tweak the strength
with `ENHANCEMENT_LEVEL=0.5 ruby examples/enhance_wav.rb …` (0.0–1.0).

## `stream_wav.rb` — real-time streaming

Streams the file frame by frame and plays the **enhanced audio live as it's produced**,
printing per-frame VAD and (if a Tyto model is present) live quality scores. Audio is
fed on the main thread; Tyto analysis runs on a separate thread so it never stalls
playback.

```bash
ruby examples/stream_wav.rb path/to/audio.wav
```

If playback is ever choppy, increase the buffer: `BUFFER_CUSHION=1.0 ruby examples/stream_wav.rb …`.

## Input format

Mono or stereo **16-bit PCM WAV**, any sample rate (stereo is downmixed; processed at
the file's native rate). The bundled model is tuned for **16 kHz** — other rates work
but only enhance up to the model's Nyquist. Convert with:

```bash
ffmpeg -i in.wav -ac 1 -ar 16000 -c:a pcm_s16le speech_16k.wav
```

Playback uses `afplay` (macOS) / `ffplay` for the streaming demo. A noisy/reverberant
speech clip shows the enhancement off best.
