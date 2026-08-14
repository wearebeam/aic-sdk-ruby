# frozen_string_literal: true

module Aicoustics
  class EnhancementResult
    attr_reader :pcm, :sample_rate, :block_size, :audio_delay_samples

    def initialize(pcm:, sample_rate:, block_size:, audio_delay_samples:)
      @pcm = pcm
      @sample_rate = sample_rate
      @block_size = block_size
      @audio_delay_samples = audio_delay_samples
    end
  end

  module Pipeline
    module_function

    def enhance_pcm(pcm_s16le, model:, license_key:, sample_rate: 16_000,
      enhancement_level: nil, otel: nil)
      model = Model.from_file(model) if model.is_a?(String)

      processor = Processor.create(model, license_key, otel: otel)
      processor.configure(sample_rate: sample_rate)

      context = processor.context
      context.enhancement_level = enhancement_level unless enhancement_level.nil?

      block_size = processor.block_size
      audio_delay = context.audio_delay

      input_samples = Pcm.s16le_to_floats(pcm_s16le)
      input_length = input_samples.length

      zero_block = Array.new(block_size, 0.0)
      enhanced = []

      input_samples.each_slice(block_size) do |slice|
        slice += Array.new(block_size - slice.length, 0.0) if slice.length < block_size
        enhanced.concat(processor.process(slice))
      end

      flush_blocks = audio_delay.zero? ? 0 : ((audio_delay + block_size - 1) / block_size)
      flush_blocks.times { enhanced.concat(processor.process(zero_block)) }

      aligned = align_output(enhanced, audio_delay, input_length)

      EnhancementResult.new(
        pcm: Pcm.floats_to_s16le(aligned),
        sample_rate: sample_rate,
        block_size: block_size,
        audio_delay_samples: audio_delay
      )
    end

    def align_output(samples, offset, length)
      return samples if offset.zero? && length.nil?

      samples[offset, length] || []
    end
  end
end
