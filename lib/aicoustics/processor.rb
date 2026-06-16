# frozen_string_literal: true

module Aicoustics
  class Processor
    attr_reader :handle, :model, :sample_rate, :num_channels, :num_frames

    def self.create(model, license_key, otel: nil)
      out = FFI::MemoryPointer.new(:pointer)
      otel_struct = otel&.to_native
      Aicoustics.check!(Native.aic_processor_create(out, model.handle, license_key.to_s, otel_struct))
      new(out.read_pointer, model, otel)
    end

    def initialize(pointer, model, otel = nil)
      @handle = FFI::AutoPointer.new(pointer, Native.method(:aic_processor_destroy))
      @model = model
      @otel = otel
    end

    def configure(sample_rate:, num_channels: 1, num_frames: nil, allow_variable_frames: false)
      num_frames ||= @model.optimal_num_frames(sample_rate)
      Aicoustics.check!(
        Native.aic_processor_initialize(@handle, sample_rate, num_channels, num_frames, allow_variable_frames)
      )
      @sample_rate = sample_rate
      @num_channels = num_channels
      @num_frames = num_frames
      self
    end

    def process_interleaved!(buffer, num_frames: @num_frames, num_channels: @num_channels)
      Aicoustics.check!(Native.aic_processor_process_interleaved(@handle, buffer, num_channels, num_frames))
      buffer
    end

    def process_sequential!(buffer, num_frames: @num_frames, num_channels: @num_channels)
      Aicoustics.check!(Native.aic_processor_process_sequential(@handle, buffer, num_channels, num_frames))
      buffer
    end

    def process_planar!(channel_buffers, num_frames: @num_frames)
      pointers = FFI::MemoryPointer.new(:pointer, channel_buffers.length)
      pointers.write_array_of_pointer(channel_buffers)
      Aicoustics.check!(Native.aic_processor_process_planar(@handle, pointers, channel_buffers.length, num_frames))
      channel_buffers
    end

    def process(floats)
      expected = @num_frames * @num_channels
      raise ArgumentError, "expected #{expected} samples, got #{floats.length}" unless floats.length == expected

      buffer = FFI::MemoryPointer.new(:float, expected)
      buffer.write_array_of_float(floats)
      process_interleaved!(buffer)
      buffer.read_array_of_float(expected)
    end

    def context
      @context ||= ProcessorContext.new(self)
    end

    def vad
      @vad ||= VadContext.new(self)
    end
  end
end
