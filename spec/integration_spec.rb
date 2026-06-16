# frozen_string_literal: true

RSpec.describe "ai-coustics SDK integration" do
  describe Aicoustics::Model do
    before { skip "set AIC_SDK_MODEL to an .aicmodel path" unless SpecSupport.model? }

    let(:model) { described_class.from_file(SpecSupport.model_path) }

    it "exposes the model id" do
      expect(model.id).to be_a(String)
      expect(model.id).not_to be_empty
    end

    it "reports an optimal sample rate and frame count" do
      rate = model.optimal_sample_rate
      expect(rate).to be > 0
      expect(model.optimal_num_frames(rate)).to be > 0
    end
  end

  describe Aicoustics::Processor do
    before { skip "set AIC_SDK_LICENSE and AIC_SDK_MODEL" unless SpecSupport.license? && SpecSupport.model? }

    let(:model) { Aicoustics::Model.from_file(SpecSupport.model_path) }
    let(:processor) do
      Aicoustics::Processor.create(model, SpecSupport.license_key).tap do |p|
        p.configure(sample_rate: 16_000, num_channels: 1)
      end
    end

    it "enhances a frame of audio in place" do
      output = processor.process(Array.new(processor.num_frames, 0.0))
      expect(output.length).to eq(processor.num_frames)
      expect(output).to all(be_a(Float))
    end

    it "exposes enhancement level through the context" do
      processor.context.enhancement_level = 0.5
      expect(processor.context.enhancement_level).to be_within(0.01).of(0.5)
    end

    it "reports a non-negative output delay" do
      expect(processor.context.output_delay).to be >= 0
    end

    it "answers VAD speech detection as a boolean" do
      processor.process(Array.new(processor.num_frames, 0.0))
      expect([true, false]).to include(processor.vad.speech_detected?)
    end
  end

  describe Aicoustics::Pipeline do
    before { skip "set AIC_SDK_LICENSE and AIC_SDK_MODEL" unless SpecSupport.license? && SpecSupport.model? }

    it "returns enhanced PCM aligned to the input length" do
      input = ([0] * 16_000).pack("s<*")
      result = Aicoustics.enhance_pcm(input, model: SpecSupport.model_path, license_key: SpecSupport.license_key, vad: true)
      expect(result.pcm.bytesize).to eq(input.bytesize)
      expect(result.speech_flags).to all(satisfy { |f| [true, false].include?(f) })
    end
  end

  describe Aicoustics::Analyzer do
    before { skip "set AIC_SDK_LICENSE and AIC_SDK_ANALYZER_MODEL (Tyto)" unless SpecSupport.license? && SpecSupport.analyzer_model? }

    let(:model) { Aicoustics::Model.from_file(SpecSupport.analyzer_model_path) }

    it "produces an analysis result with all scores populated" do
      analyzer = described_class.create(model, SpecSupport.license_key)
      analyzer.configure(sample_rate: 16_000, num_channels: 1)
      buffer = FFI::MemoryPointer.new(:float, analyzer.num_frames)
      analyzer.buffer_interleaved!(buffer)
      result = analyzer.analyze
      expect(result.to_h.keys).to match_array(Aicoustics::AnalysisResult::ATTRIBUTES)
      expect(result.risk_score).to be_a(Float)
    end
  end
end
