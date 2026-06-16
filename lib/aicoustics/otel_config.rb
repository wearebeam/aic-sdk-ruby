# frozen_string_literal: true

module Aicoustics
  class OtelConfig
    attr_reader :enable, :session_id, :export_interval_ms

    def initialize(enable: true, session_id: nil, export_interval_ms: 0)
      @enable = enable
      @session_id = session_id
      @export_interval_ms = export_interval_ms
    end

    def to_native
      struct = Native::OtelConfig.new
      struct[:enable] = @enable
      if @session_id
        @session_id_buffer = FFI::MemoryPointer.from_string(@session_id.to_s)
        struct[:session_id] = @session_id_buffer
      else
        struct[:session_id] = FFI::Pointer::NULL
      end
      struct[:export_interval_ms] = @export_interval_ms
      @struct = struct
    end
  end
end
