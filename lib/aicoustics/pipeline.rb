# frozen_string_literal: true

module Aicoustics
  class EnhancementResult
    attr_reader :pcm, :speech_flags, :sample_rate, :num_frames, :output_delay_samples

    def initialize(pcm:, sample_rate:, num_frames:, output_delay_samples:, speech_flags: nil)
      @pcm = pcm
      @sample_rate = sample_rate
      @num_frames = num_frames
      @output_delay_samples = output_delay_samples
      @speech_flags = speech_flags
    end

    def any_speech?
      return nil if @speech_flags.nil?

      @speech_flags.any?
    end
  end

  module Pipeline
    module_function

    def enhance_pcm(pcm_s16le, model:, license_key:, sample_rate: 16_000, num_channels: 1,
      enhancement_level: nil, vad: false, vad_sensitivity: nil, otel: nil)
      model = Model.from_file(model) if model.is_a?(String)

      processor = Processor.create(model, license_key, otel: otel)
      processor.configure(sample_rate: sample_rate, num_channels: num_channels)

      context = processor.context
      context.enhancement_level = enhancement_level unless enhancement_level.nil?

      vad_context = vad ? processor.vad : nil
      vad_context.sensitivity = vad_sensitivity if vad_context && !vad_sensitivity.nil?

      num_frames = processor.num_frames
      frame_samples = num_frames * num_channels
      output_delay = context.output_delay

      input_samples = Pcm.s16le_to_floats(pcm_s16le)
      input_length = input_samples.length

      zero_frame = Array.new(frame_samples, 0.0)
      enhanced = []
      speech_flags = vad ? [] : nil

      input_samples.each_slice(frame_samples) do |slice|
        slice += Array.new(frame_samples - slice.length, 0.0) if slice.length < frame_samples
        enhanced.concat(processor.process(slice))
        speech_flags << vad_context.speech_detected? if vad
      end

      flush_frames = output_delay.zero? ? 0 : ((output_delay + num_frames - 1) / num_frames)
      flush_frames.times do
        enhanced.concat(processor.process(zero_frame))
        speech_flags << vad_context.speech_detected? if vad
      end

      aligned = align_output(enhanced, output_delay * num_channels, input_length)

      EnhancementResult.new(
        pcm: Pcm.floats_to_s16le(aligned),
        sample_rate: sample_rate,
        num_frames: num_frames,
        output_delay_samples: output_delay,
        speech_flags: speech_flags
      )
    end

    def align_output(samples, offset, length)
      return samples if offset.zero? && length.nil?

      samples[offset, length] || []
    end
  end
end
