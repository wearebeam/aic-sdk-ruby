# frozen_string_literal: true

module Aicoustics
  class Analyzer
    attr_reader :collector_handle, :analyzer_handle, :model, :sample_rate, :num_channels, :num_frames

    def self.create(model, license_key)
      collector_out = FFI::MemoryPointer.new(:pointer)
      analyzer_out = FFI::MemoryPointer.new(:pointer)
      Aicoustics.check!(
        Native.aic_analyzer_pair_create(collector_out, analyzer_out, model.handle, license_key.to_s)
      )
      new(collector_out.read_pointer, analyzer_out.read_pointer, model)
    end

    def initialize(collector_pointer, analyzer_pointer, model)
      @collector_handle = FFI::AutoPointer.new(collector_pointer, Native.method(:aic_collector_destroy))
      @analyzer_handle = FFI::AutoPointer.new(analyzer_pointer, Native.method(:aic_analyzer_destroy))
      @model = model
    end

    def configure(sample_rate:, num_channels: 1, num_frames: nil, allow_variable_frames: false)
      num_frames ||= @model.optimal_num_frames(sample_rate)
      Aicoustics.check!(
        Native.aic_collector_initialize(@collector_handle, sample_rate, num_channels, num_frames, allow_variable_frames)
      )
      @sample_rate = sample_rate
      @num_channels = num_channels
      @num_frames = num_frames
      self
    end

    def buffer_interleaved!(buffer, num_frames: @num_frames, num_channels: @num_channels)
      Aicoustics.check!(Native.aic_collector_buffer_interleaved(@collector_handle, buffer, num_channels, num_frames))
      self
    end

    def buffer_sequential!(buffer, num_frames: @num_frames, num_channels: @num_channels)
      Aicoustics.check!(Native.aic_collector_buffer_sequential(@collector_handle, buffer, num_channels, num_frames))
      self
    end

    def analyze
      result = Native::AnalysisResult.new
      Aicoustics.check!(Native.aic_analyzer_analyze_buffered(@analyzer_handle, result))
      AnalysisResult.from_native(result)
    end

    def reset
      Aicoustics.check!(Native.aic_analyzer_reset(@analyzer_handle))
      self
    end

    def update_bearer_token(token)
      Aicoustics.check!(Native.aic_analyzer_update_bearer_token(@analyzer_handle, token.to_s))
      self
    end
  end
end
