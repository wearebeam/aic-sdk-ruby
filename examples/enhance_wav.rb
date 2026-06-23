#!/usr/bin/env ruby
# frozen_string_literal: true

# Enhance a WAV, then play the original and enhanced versions. Setup & usage:
# see examples/README.md.

require_relative "../lib/aicoustics"
require_relative "support"

Support.load_env(__dir__)
input = ARGV.fetch(0)
pcm, rate = Support.read_wav_mono(input)

result = Aicoustics.enhance_pcm(
  pcm,
  model: ENV.fetch("AIC_SDK_MODEL", File.expand_path("../spec/fixtures/model.aicmodel", __dir__)),
  license_key: ENV.fetch("AIC_SDK_LICENSE"),
  sample_rate: rate,
  vad: true
)

base = File.join(__dir__, "output", File.basename(input, ".*"))
Support.write_wav("#{base}.original.wav", pcm, rate)
Support.write_wav("#{base}.enhanced.wav", result.pcm, rate)

Support.play("#{base}.original.wav")
Support.play("#{base}.enhanced.wav")
