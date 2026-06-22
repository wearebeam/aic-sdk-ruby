# frozen_string_literal: true

module Aicoustics
  class AnalysisResult
    ATTRIBUTES = %i[
      risk_score
      speaker_reverb
      speaker_loudness
      interfering_speech
      media_speech
      noise
      packet_loss
    ].freeze

    attr_reader(*ATTRIBUTES)

    # Build from a hash of attribute => value (used by the C extension's #analyze).
    def self.from_h(hash)
      new(**ATTRIBUTES.to_h { |name| [name, hash[name]] })
    end

    def initialize(**values)
      ATTRIBUTES.each { |name| instance_variable_set(:"@#{name}", values[name]) }
    end

    def to_h
      ATTRIBUTES.to_h { |name| [name, public_send(name)] }
    end
  end
end
