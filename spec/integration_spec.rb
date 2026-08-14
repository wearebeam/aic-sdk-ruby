# frozen_string_literal: true

RSpec.describe "ai-coustics SDK integration" do
  describe Aicoustics::Model do
    before { skip "set AIC_SDK_MODEL to an .aicmodel path" unless SpecSupport.model? }

    let(:model) { described_class.from_file(SpecSupport.model_path) }

    it "exposes the model id" do
      expect(model.id).to be_a(String)
      expect(model.id).not_to be_empty
    end

    it "reports an optimal sample rate and block size" do
      rate = model.optimal_sample_rate
      expect(rate).to be > 0
      expect(model.optimal_block_size(rate)).to be > 0
    end
  end

  describe Aicoustics::Processor do
    before { skip "set AIC_SDK_LICENSE and AIC_SDK_MODEL" unless SpecSupport.license? && SpecSupport.model? }

    let(:model) { Aicoustics::Model.from_file(SpecSupport.model_path) }
    let(:processor) do
      Aicoustics::Processor.create(model, SpecSupport.license_key).tap do |p|
        p.configure(sample_rate: 16_000)
      end
    end

    it "enhances a block of audio in place" do
      output = processor.process(Array.new(processor.block_size, 0.0))
      expect(output.length).to eq(processor.block_size)
      expect(output).to all(be_a(Float))
    end

    it "exposes enhancement level through the context" do
      processor.context.enhancement_level = 0.5
      expect(processor.context.enhancement_level).to be_within(0.01).of(0.5)
    end

    it "reports a non-negative audio delay" do
      expect(processor.context.audio_delay).to be >= 0
    end
  end

  describe Aicoustics::Vad do
    before { skip "set AIC_SDK_LICENSE and AIC_SDK_VAD_MODEL" unless SpecSupport.license? && SpecSupport.vad_model? }

    let(:model) { Aicoustics::Model.from_file(SpecSupport.vad_model_path) }
    let(:vad) do
      described_class.create(model, SpecSupport.license_key).tap do |v|
        v.configure(sample_rate: 16_000)
      end
    end

    it "answers speech detection as a boolean after processing a block" do
      vad.process!(Array.new(vad.block_size, 0.0).pack("f*"))
      expect([true, false]).to include(vad.context.speech_detected?)
    end

    it "reports a raw probability and a non-negative prediction delay" do
      vad.process!(Array.new(vad.block_size, 0.0).pack("f*"))
      expect(vad.context.raw_vad_probability).to be_between(0.0, 1.0)
      expect(vad.context.prediction_delay).to be >= 0
    end
  end

  describe Aicoustics::Pipeline do
    before { skip "set AIC_SDK_LICENSE and AIC_SDK_MODEL" unless SpecSupport.license? && SpecSupport.model? }

    it "returns enhanced PCM aligned to the input length" do
      input = ([0] * 16_000).pack("s<*")
      result = Aicoustics.enhance_pcm(input, model: SpecSupport.model_path, license_key: SpecSupport.license_key)
      expect(result.pcm.bytesize).to eq(input.bytesize)
    end
  end

  describe Aicoustics::Analyzer do
    before { skip "set AIC_SDK_LICENSE and AIC_SDK_ANALYZER_MODEL (Tyto)" unless SpecSupport.license? && SpecSupport.analyzer_model? }

    let(:model) { Aicoustics::Model.from_file(SpecSupport.analyzer_model_path) }

    it "produces an analysis result with all scores populated" do
      analyzer = described_class.create(model, SpecSupport.license_key)
      analyzer.configure(sample_rate: 16_000)
      buffer = Array.new(analyzer.block_size, 0.0).pack("f*") # mono float32 block
      analyzer.buffer!(buffer)
      result = analyzer.analyze
      expect(result.to_h.keys).to match_array(Aicoustics::AnalysisResult::ATTRIBUTES)
      expect(result.risk_score).to be_a(Float)
    end
  end
end
