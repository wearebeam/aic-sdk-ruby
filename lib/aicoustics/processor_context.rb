# frozen_string_literal: true

module Aicoustics
  class ProcessorContext
    attr_reader :handle

    def initialize(processor)
      @processor = processor
      out = FFI::MemoryPointer.new(:pointer)
      Aicoustics.check!(Native.aic_processor_context_create(out, processor.handle))
      @handle = FFI::AutoPointer.new(out.read_pointer, Native.method(:aic_processor_context_destroy))
    end

    def reset
      Aicoustics.check!(Native.aic_processor_context_reset(@handle))
      self
    end

    def set_parameter(parameter, value)
      Aicoustics.check!(Native.aic_processor_context_set_parameter(@handle, parameter, value))
      value
    end

    def get_parameter(parameter)
      out = FFI::MemoryPointer.new(:float)
      Aicoustics.check!(Native.aic_processor_context_get_parameter(@handle, parameter, out))
      out.read_float
    end

    def enhancement_level
      get_parameter(:enhancement_level)
    end

    def enhancement_level=(value)
      set_parameter(:enhancement_level, value.to_f)
    end

    def bypass?
      get_parameter(:bypass) >= 0.5
    end

    def bypass=(enabled)
      set_parameter(:bypass, enabled ? 1.0 : 0.0)
    end

    def output_delay
      out = FFI::MemoryPointer.new(:size_t)
      Aicoustics.check!(Native.aic_processor_context_get_output_delay(@handle, out))
      out.read(:size_t)
    end

    def update_bearer_token(token)
      Aicoustics.check!(Native.aic_processor_context_update_bearer_token(@handle, token.to_s))
      self
    end
  end
end
