#!/usr/bin/env ruby
# frozen_string_literal: true

# A/B demo: enhance a WAV through the ai-coustics SDK, then play the original and
# enhanced versions back to back so you can hear the difference.
#
#   bundle exec rake compile
#   cp examples/.env.example examples/.env    # add AIC_SDK_LICENSE
#   ruby examples/enhance_wav.rb path/to/audio.wav
#
# WAV/PCM and .env handling live in examples/support.rb so this file stays focused
# on the SDK. Input may be mono or stereo 16-bit PCM at any rate (downmixed to mono,
# processed at its native rate); 16 kHz matches the bundled model best.

require "rbconfig"
require "fileutils"
require_relative "../lib/aicoustics"
require_relative "support"

def play(path, label)
  puts "  ▶ playing #{label} (#{File.basename(path)})…"
  player =
    if RbConfig::CONFIG["host_os"] =~ /darwin/ then ["afplay", path]
    elsif system("command -v ffplay > /dev/null 2>&1") then ["ffplay", "-autoexit", "-nodisp", "-loglevel", "quiet", path]
    elsif system("command -v aplay  > /dev/null 2>&1") then ["aplay", "-q", path]
    end
  player ? system(*player) : puts("  (no audio player found — open #{path} manually)")
end

Support.load_env(__dir__)

input = ARGV[0]
abort "usage: ruby examples/enhance_wav.rb <input.wav>" unless input && File.exist?(input)
license = ENV["AIC_SDK_LICENSE"]
abort "set AIC_SDK_LICENSE in examples/.env (see examples/.env.example)" if license.to_s.empty?
model_path = ENV.fetch("AIC_SDK_MODEL", File.expand_path("../spec/fixtures/model.aicmodel", __dir__))
abort "model not found at #{model_path} — run: URL=... rake model:fetch" unless File.exist?(model_path)

pcm, rate = Support.read_wav_mono(input)
puts "Loaded #{File.basename(input)}: #{rate} Hz, #{pcm.bytesize / 2} samples (#{(pcm.bytesize / 2.0 / rate).round(2)} s)"
puts "Model: #{File.basename(model_path)} (SDK #{Aicoustics.sdk_version})"
warn "  note: input is #{rate} Hz; the bundled model is optimal at 16 kHz" unless rate == 16_000

result = Aicoustics.enhance_pcm(
  pcm,
  model: model_path,
  license_key: license,
  sample_rate: rate,
  enhancement_level: Float(ENV.fetch("ENHANCEMENT_LEVEL", "1.0")),
  vad: true
)

out_dir = File.expand_path("output", __dir__)
FileUtils.mkdir_p(out_dir)
base = File.basename(input, ".*")
original_out = File.join(out_dir, "#{base}.original-mono.wav")
enhanced_out = File.join(out_dir, "#{base}.enhanced.wav")
Support.write_wav(original_out, pcm, rate)
Support.write_wav(enhanced_out, result.pcm, rate)

spoke = result.speech_flags&.count(true)
puts "Enhanced. output delay: #{result.output_delay_samples} samples" \
     "#{spoke ? ", VAD: #{spoke}/#{result.speech_flags.length} frames had speech" : ""}"
puts "Wrote:\n  #{original_out}\n  #{enhanced_out}\n\n"

play(original_out, "ORIGINAL (downmixed mono)")
print "  press Enter to play the ENHANCED version… "
$stdin.gets
play(enhanced_out, "ENHANCED")
puts "\nDone. Re-listen any time:\n  original: #{original_out}\n  enhanced: #{enhanced_out}"
