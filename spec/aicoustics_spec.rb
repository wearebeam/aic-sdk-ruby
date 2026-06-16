# frozen_string_literal: true

RSpec.describe Aicoustics do
  describe ".sdk_version" do
    it "returns the vendored SDK version string" do
      expect(Aicoustics.sdk_version).to match(/\A\d+\.\d+\.\d+/)
    end

    it "matches the version the gem declares it vendors" do
      expect(Aicoustics.sdk_version).to eq(Aicoustics::SDK_VERSION)
    end
  end

  describe ".compatible_model_version" do
    it "returns a positive integer" do
      expect(Aicoustics.compatible_model_version).to be_a(Integer)
      expect(Aicoustics.compatible_model_version).to be > 0
    end
  end

  describe ".library_path" do
    it "points at an existing vendored native library" do
      expect(File.exist?(Aicoustics.library_path)).to be(true)
    end
  end

  describe Aicoustics::Native do
    it "resolves a known platform slug" do
      expect(described_class.platform_slug).to match(/\A(aarch64|x86_64)-(darwin|linux)\z/)
    end
  end
end
