# frozen_string_literal: true

module Aicoustics
  module Pcm
    INT16_TO_FLOAT = 32_768.0
    FLOAT_TO_INT16 = 32_767.0
    INT16_MIN = -32_768
    INT16_MAX = 32_767

    module_function

    def s16le_to_floats(bytes)
      bytes.unpack("s<*").map! { |sample| sample / INT16_TO_FLOAT }
    end

    def floats_to_s16le(floats)
      floats.map { |value|
        unit = clamp_unit(value)
        scaled = (unit * (unit.negative? ? INT16_TO_FLOAT : FLOAT_TO_INT16)).round
        scaled < INT16_MIN ? INT16_MIN : (scaled > INT16_MAX ? INT16_MAX : scaled)
      }.pack("s<*")
    end

    def clamp_unit(value)
      return -1.0 if value < -1.0
      return 1.0 if value > 1.0

      value
    end
  end
end
