# frozen_string_literal: true

module Aicoustics
  class VadContext
    attr_reader :handle

    def initialize(processor)
      @processor = processor
      out = FFI::MemoryPointer.new(:pointer)
      Aicoustics.check!(Native.aic_vad_context_create(out, processor.handle))
      @handle = FFI::AutoPointer.new(out.read_pointer, Native.method(:aic_vad_context_destroy))
    end

    def speech_detected?
      out = FFI::MemoryPointer.new(:uint8)
      Aicoustics.check!(Native.aic_vad_context_is_speech_detected(@handle, out))
      out.read_uint8 != 0
    end

    def set_parameter(parameter, value)
      Aicoustics.check!(Native.aic_vad_context_set_parameter(@handle, parameter, value))
      value
    end

    def get_parameter(parameter)
      out = FFI::MemoryPointer.new(:float)
      Aicoustics.check!(Native.aic_vad_context_get_parameter(@handle, parameter, out))
      out.read_float
    end

    def sensitivity
      get_parameter(:sensitivity)
    end

    def sensitivity=(value)
      set_parameter(:sensitivity, value.to_f)
    end

    def speech_hold_duration
      get_parameter(:speech_hold_duration)
    end

    def speech_hold_duration=(value)
      set_parameter(:speech_hold_duration, value.to_f)
    end

    def minimum_speech_duration
      get_parameter(:minimum_speech_duration)
    end

    def minimum_speech_duration=(value)
      set_parameter(:minimum_speech_duration, value.to_f)
    end
  end
end
