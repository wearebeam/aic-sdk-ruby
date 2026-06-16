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

    def self.from_native(struct)
      new(**ATTRIBUTES.to_h { |name| [name, struct[name]] })
    end

    def initialize(**values)
      ATTRIBUTES.each { |name| instance_variable_set(:"@#{name}", values[name]) }
    end

    def to_h
      ATTRIBUTES.to_h { |name| [name, public_send(name)] }
    end
  end
end
