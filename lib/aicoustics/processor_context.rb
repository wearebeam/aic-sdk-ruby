# frozen_string_literal: true

module Aicoustics
  # Reopens the C-defined ProcessorContext to add named-parameter sugar on top of
  # the native #get_parameter/#set_parameter primitives.
  class ProcessorContext
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
  end
end
