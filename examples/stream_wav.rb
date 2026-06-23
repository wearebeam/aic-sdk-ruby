#!/usr/bin/env ruby
# frozen_string_literal: true

# Real-time demo, written the way you'd run it on a server: stream a WAV through the
# ai-coustics SDK frame by frame, play the ENHANCED audio live, and print VAD + Tyto
# findings alongside — on the zero-copy hot path.
#
#   bundle exec rake compile
#   cp examples/.env.example examples/.env    # add AIC_SDK_LICENSE
#   ruby examples/stream_wav.rb path/to/audio.wav
#
# Performance: the SDK processes float32. We convert int16 -> float32 ONCE up front,
# then the per-frame loop only byte-slices and calls process_interleaved!, which
# mutates the frame's bytes in place (no Ruby Float boxing, no copies). The enhanced
# float32 bytes go straight to ffplay (-f f32le) and to Tyto — no conversion back.
# (A live server would convert each incoming int16 frame once, instead of up front.)
#
# Threading: audio is fed on the main thread (paced to real time with a small buffer
# cushion); Tyto analysis runs on its own thread so its heavy inference never stalls
# playback. Both overlap because the SDK releases the GVL during process/analyze.

require_relative "../lib/aicoustics"
require_relative "support"

BYTES_PER_FLOAT = 4

def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

Support.load_env(__dir__)

input = ARGV[0]
abort "usage: ruby examples/stream_wav.rb <input.wav>" unless input && File.exist?(input)
license = ENV["AIC_SDK_LICENSE"]
abort "set AIC_SDK_LICENSE in examples/.env" if license.to_s.empty?

enh_model_path  = ENV.fetch("AIC_SDK_MODEL", File.expand_path("../spec/fixtures/model.aicmodel", __dir__))
tyto_model_path = ENV["AIC_SDK_ANALYZER_MODEL"] || File.expand_path("../spec/fixtures/tyto.aicmodel", __dir__)
level   = Float(ENV.fetch("ENHANCEMENT_LEVEL", "1.0"))
cushion = Float(ENV.fetch("BUFFER_CUSHION", "0.4")) # seconds of audio kept ahead of the log clock

pcm, rate = Support.read_wav_mono(input)
# One-time int16 -> normalized float32 little-endian buffer; the hot loop never re-converts.
float_audio = Aicoustics::Pcm.s16le_to_floats(pcm).pack("e*")

processor = Aicoustics::Processor.create(Aicoustics::Model.from_file(enh_model_path), license)
processor.configure(sample_rate: rate, num_channels: 1)
processor.context.enhancement_level = level
vad = processor.vad
frame_bytes = processor.num_frames * BYTES_PER_FLOAT

analyzer = nil
analyzer_bytes = nil
if File.exist?(tyto_model_path)
  analyzer = Aicoustics::Analyzer.create(Aicoustics::Model.from_file(tyto_model_path), license)
  analyzer.configure(sample_rate: rate, num_channels: 1)
  analyzer_bytes = analyzer.num_frames * BYTES_PER_FLOAT
end

total_frames = (float_audio.bytesize.to_f / frame_bytes).ceil
puts "Streaming #{File.basename(input)} — #{rate} Hz, #{(float_audio.bytesize / BYTES_PER_FLOAT / rate.to_f).round(1)} s, " \
     "enhancement_level=#{level}#{analyzer ? ', Tyto on' : ''}"
puts "Playing the ENHANCED audio live (zero-copy hot path; audio + Tyto on separate threads).\n\n"

player = IO.popen(["ffplay", "-f", "f32le", "-ar", rate.to_s, "-ch_layout", "mono",
                   "-nodisp", "-autoexit", "-loglevel", "quiet", "-i", "pipe:0"], "wb")
player.sync = true

# Tyto runs on its own thread, consuming the same enhanced float32 bytes.
scores = nil
scores_lock = Mutex.new
tyto_queue = Thread::Queue.new
analyzer_thread =
  if analyzer
    chunks_per_analyze = [(0.5 * rate * BYTES_PER_FLOAT / analyzer_bytes).round, 1].max
    Thread.new do
      pending = +"".b
      chunks = 0
      while (enhanced = tyto_queue.pop) # nil once the queue is closed and drained
        pending << enhanced
        while pending.bytesize >= analyzer_bytes
          analyzer.buffer_interleaved!(pending.byteslice(0, analyzer_bytes))
          pending = pending.byteslice(analyzer_bytes..) || +"".b
          chunks += 1
        end
        if chunks >= chunks_per_analyze
          chunks = 0
          result = analyzer.analyze.to_h
          scores_lock.synchronize { scores = result }
        end
      end
    end
  end

frame_dur = (frame_bytes / BYTES_PER_FLOAT) / rate.to_f
speech_frames = 0
offset = 0
index = 0
started = now

begin
  while offset < float_audio.bytesize
    frame = float_audio.byteslice(offset, frame_bytes)
    frame = frame.ljust(frame_bytes, "\x00").b if frame.bytesize < frame_bytes # pad final frame
    frame = +frame # ensure a private, mutable buffer for in-place processing

    processor.process_interleaved!(frame)   # enhance the float32 frame IN PLACE (zero-copy)
    player.write(frame)                      # already float32 bytes — straight to the speakers
    speech = vad.speech_detected?
    speech_frames += 1 if speech
    tyto_queue.push(frame) if analyzer       # same bytes feed Tyto (read-only there)

    current = scores_lock.synchronize { scores }
    tyto = current ? "risk %.2f  noise %.2f  reverb %.2f  loud %.2f" %
                     [current[:risk_score], current[:noise], current[:speaker_reverb], current[:speaker_loudness]] : "warming up…"
    printf("\r[%6.2fs] %s | %s   ", index * frame_dur, speech ? "🔊 SPEECH " : "·· silence", tyto)

    # stay `cushion` seconds ahead of real time so ffplay always has a buffer
    ahead = (started + (index + 1) * frame_dur - cushion) - now
    sleep(ahead) if ahead > 0

    offset += frame_bytes
    index += 1
  end
rescue Errno::EPIPE
  warn "\nplayback stopped (ffplay closed)"
ensure
  tyto_queue.close
  analyzer_thread&.join
  player.close
end

puts "\n\nDone. Speech in #{speech_frames}/#{total_frames} frames (#{(100.0 * speech_frames / total_frames).round}%)."
puts "Final Tyto: #{scores}" if scores
