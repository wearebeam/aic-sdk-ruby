# frozen_string_literal: true

module Aicoustics
  # Reopens the C-defined VadContext to add named-parameter sugar on top of the
  # native #get_parameter/#set_parameter primitives.
  class VadContext
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
