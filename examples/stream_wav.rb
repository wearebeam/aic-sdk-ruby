#!/usr/bin/env ruby
# frozen_string_literal: true

# Stream a WAV through the SDK and play the enhanced audio live, printing per-frame
# VAD + live Tyto scores. Zero-copy hot path (no per-frame allocations); audio is fed
# on the main thread, Tyto analysis on its own. Setup & usage: examples/README.md.

require_relative "../lib/aicoustics"
require_relative "support"

FLOAT32_BYTES = 4 # the SDK works in 32-bit floats; we slice the audio in bytes

Support.load_env(__dir__)
license  = ENV.fetch("AIC_SDK_LICENSE")
fixtures = File.expand_path("../spec/fixtures", __dir__)
pcm, rate = Support.read_wav_mono(ARGV.fetch(0))
float_audio = Aicoustics::Pcm.s16le_to_floats(pcm).pack("e*") # int16 -> float32 once, up front

enhancer = Aicoustics::Processor.create(Aicoustics::Model.from_file("#{fixtures}/model.aicmodel"), license)
enhancer.configure(sample_rate: rate, num_channels: 1)
analyzer = Aicoustics::Analyzer.create(Aicoustics::Model.from_file("#{fixtures}/tyto.aicmodel"), license)
analyzer.configure(sample_rate: rate, num_channels: 1)

# Each model consumes a fixed block of `num_frames` samples per call; as bytes (mono):
enhance_block_bytes = enhancer.num_frames * FLOAT32_BYTES
analyze_block_bytes = analyzer.num_frames * FLOAT32_BYTES
seconds_per_block   = enhancer.num_frames / rate.to_f

player = IO.popen(%W[ffplay -f f32le -ar #{rate} -ch_layout mono -nodisp -autoexit -loglevel quiet -i pipe:0], "wb")
player.sync = true

# Tyto analysis on its own thread so its inference never stalls playback.
scores = nil
scores_lock = Mutex.new
to_analyze = Thread::Queue.new
analysis_thread = Thread.new do
  pending = +"".b
  blocks_buffered = 0
  while (enhanced = to_analyze.pop)
    pending << enhanced
    while pending.bytesize >= analyze_block_bytes
      analyzer.buffer_interleaved!(pending.slice!(0, analyze_block_bytes))
      blocks_buffered += 1
    end
    if blocks_buffered >= 16 # re-analyze ~4x/second
      blocks_buffered = 0
      latest = analyzer.analyze.to_h
      scores_lock.synchronize { scores = latest }
    end
  end
end

clock = -> { Process.clock_gettime(Process::CLOCK_MONOTONIC) }
started_at = clock.call

float_audio.bytesize.fdiv(enhance_block_bytes).ceil.times do |i|
  block = +float_audio.byteslice(i * enhance_block_bytes, enhance_block_bytes).ljust(enhance_block_bytes, "\x00")
  enhancer.process_interleaved!(block) # enhance in place — zero copy
  player.write(block)                   # float32 bytes straight to ffplay
  to_analyze.push(block)                # same bytes feed Tyto

  current = scores_lock.synchronize { scores }
  printf("\r[%5.1fs] %-7s | %s", i * seconds_per_block, enhancer.vad.speech_detected? ? "SPEECH" : "silence",
         current ? "risk %.2f  noise %.2f  reverb %.2f" % current.values_at(:risk_score, :noise, :speaker_reverb) : "")

  # keep ~0.4 s of audio buffered ahead of real time so playback stays smooth
  sleep([started_at + (i + 1) * seconds_per_block - 0.4 - clock.call, 0].max)
end

to_analyze.close
analysis_thread.join
player.close
puts
