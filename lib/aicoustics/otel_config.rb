# frozen_string_literal: true

module Aicoustics
  # Plain value object for OpenTelemetry configuration. The C extension reads
  # #enable, #session_id, and #export_interval_ms when creating a processor.
  class OtelConfig
    attr_reader :enable, :session_id, :export_interval_ms

    def initialize(enable: true, session_id: nil, export_interval_ms: 0)
      @enable = enable
      @session_id = session_id
      @export_interval_ms = export_interval_ms
    end
  end
end
