# Examples

Two small demos for hearing the ai-coustics SDK work on a WAV file. WAV/PCM, `.env`,
and playback are factored into `support.rb`, so each demo reads as `filename → SDK`.

## Setup

```bash
rake vendor:fetch                       # fetch the native libs
bundle exec rake compile                # build the C extension
cp examples/.env.example examples/.env  # then add your AIC_SDK_LICENSE
```

The demos use the bundled models in `spec/fixtures/` (`model.aicmodel` for
enhancement, `tyto.aicmodel` for analysis), fetched via `URL=... rake model:fetch`.

## Run

```bash
ruby examples/enhance_wav.rb <file.wav>   # enhance, then play original vs enhanced
ruby examples/stream_wav.rb <file.wav>    # stream live: enhanced audio + VAD + Tyto scores
```

Both read the key from `examples/.env`. `enhance_wav.rb` writes the two WAVs to
`examples/output/`; `stream_wav.rb` plays the enhanced stream in real time and prints
findings as it goes. `stream_wav.rb` needs `ffplay` (from ffmpeg).

## Input format

Mono or stereo **16-bit PCM WAV**, any sample rate (stereo is downmixed, processed at
the file's native rate). The bundled model is tuned for **16 kHz**; convert others with:

```bash
ffmpeg -i in.wav -ac 1 -ar 16000 -c:a pcm_s16le speech_16k.wav
```

A noisy/reverberant speech clip shows the enhancement off best.
