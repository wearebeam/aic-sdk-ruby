#!/usr/bin/env ruby
# frozen_string_literal: true

# Stream a WAV through the SDK and play the enhanced audio live, printing per-frame
# VAD + live Tyto scores. Zero-copy hot path (no per-frame allocations); audio is fed
# on the main thread, Tyto analysis on its own. Setup & usage: examples/README.md.

require_relative "../lib/aicoustics"
require_relative "support"

Support.load_env(__dir__)
license  = ENV.fetch("AIC_SDK_LICENSE")
fixtures = File.expand_path("../spec/fixtures", __dir__)
pcm, rate = Support.read_wav_mono(ARGV.fetch(0))
audio = Aicoustics::Pcm.s16le_to_floats(pcm).pack("e*") # int16 -> float32 once, up front

processor = Aicoustics::Processor.create(Aicoustics::Model.from_file("#{fixtures}/model.aicmodel"), license)
processor.configure(sample_rate: rate, num_channels: 1)
analyzer = Aicoustics::Analyzer.create(Aicoustics::Model.from_file("#{fixtures}/tyto.aicmodel"), license)
analyzer.configure(sample_rate: rate, num_channels: 1)

frame  = processor.num_frames * 4 # bytes per mono float32 frame
abytes = analyzer.num_frames * 4
player = IO.popen(%W[ffplay -f f32le -ar #{rate} -ch_layout mono -nodisp -autoexit -loglevel quiet -i pipe:0], "wb")
player.sync = true

# Tyto analysis on its own thread so its inference never stalls playback.
scores = nil
lock = Mutex.new
queue = Thread::Queue.new
tyto = Thread.new do
  buffer = +"".b
  fed = 0
  while (chunk = queue.pop)
    buffer << chunk
    (analyzer.buffer_interleaved!(buffer.slice!(0, abytes)); fed += 1) while buffer.bytesize >= abytes
    (lock.synchronize { scores = analyzer.analyze.to_h }; fed = 0) if fed >= 16 # ~4x/sec
  end
end

clock = -> { Process.clock_gettime(Process::CLOCK_MONOTONIC) }
dur = processor.num_frames / rate.to_f
start = clock.call

audio.bytesize.fdiv(frame).ceil.times do |i|
  chunk = +audio.byteslice(i * frame, frame).ljust(frame, "\x00")
  processor.process_interleaved!(chunk) # enhance in place — zero copy
  player.write(chunk)                   # float32 bytes straight to ffplay
  queue.push(chunk)                     # same bytes feed Tyto
  s = lock.synchronize { scores }
  printf("\r[%5.1fs] %-7s | %s", i * dur, processor.vad.speech_detected? ? "SPEECH" : "silence",
         s ? "risk %.2f  noise %.2f  reverb %.2f" % s.values_at(:risk_score, :noise, :speaker_reverb) : "")
  sleep([start + (i + 1) * dur - 0.4 - clock.call, 0].max) # keep ~0.4 s buffered ahead
end

queue.close
tyto.join
player.close
puts
