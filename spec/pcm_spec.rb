# frozen_string_literal: true

RSpec.describe Aicoustics::Pcm do
  describe ".s16le_to_floats" do
    it "normalises 16-bit samples into the [-1.0, 1.0] range" do
      bytes = [0, 16_384, -16_384, 32_767, -32_768].pack("s<*")
      expect(described_class.s16le_to_floats(bytes)).to eq([0.0, 0.5, -0.5, 32_767 / 32_768.0, -1.0])
    end

    it "returns an empty array for empty input" do
      expect(described_class.s16le_to_floats("")).to eq([])
    end
  end

  describe ".floats_to_s16le" do
    it "clamps values outside [-1.0, 1.0]" do
      samples = described_class.floats_to_s16le([1.5, -1.5]).unpack("s<*")
      expect(samples).to eq([32_767, -32_768])
    end

    it "round-trips mid-scale values without drift" do
      bytes = [0, 16_384, -16_384, 8_000, -8_000].pack("s<*")
      floats = described_class.s16le_to_floats(bytes)
      expect(described_class.floats_to_s16le(floats).unpack("s<*")).to eq([0, 16_384, -16_384, 8_000, -8_000])
    end
  end
end
