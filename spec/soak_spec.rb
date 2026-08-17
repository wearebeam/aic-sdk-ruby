# frozen_string_literal: true

# Native-memory soak/stress suite. Specs alone can't catch the C extension's
# real failure modes — leaks, GC races, use-after-free — because they create a
# handful of handles and exit. These phases create thousands, force GC into
# the worst positions, and hammer the threaded shape production actually uses.
# Excluded from normal runs (slow, needs license + all three models); enable
# with AIC_SDK_SOAK=1. See .github/workflows/ci.yml for the CI wiring.
RSpec.describe "native memory soak", :soak do
  before(:all) do
    skip "set AIC_SDK_SOAK=1 plus AIC_SDK_LICENSE and model paths" unless SpecSupport.soak?
  end

  def rss_mb = `ps -o rss= -p #{Process.pid}`.to_i / 1024.0

  let(:license) { SpecSupport.license_key }

  it "keeps RSS flat while churning through handle lifecycles" do
    enhancer_model = Aicoustics::Model.from_file(SpecSupport.model_path)
    tyto_model = Aicoustics::Model.from_file(SpecSupport.analyzer_model_path)
    vad_model = Aicoustics::Model.from_file(SpecSupport.vad_model_path)

    # Warm up every code path once so one-off allocations (lazy SDK init,
    # method caches) don't count against the growth budget.
    exercise = lambda do |analyze: false|
      processor = Aicoustics::Processor.create(enhancer_model, license)
      processor.configure(sample_rate: 16_000)
      processor.process!(Array.new(processor.block_size, 0.1).pack("f*"))
      processor.context.audio_delay

      vad = Aicoustics::Vad.create(vad_model, license)
      vad.configure(sample_rate: 16_000)
      vad.process!(Array.new(vad.block_size, 0.1).pack("f*"))
      vad.context.speech_detected?

      analyzer = Aicoustics::Analyzer.create(tyto_model, license)
      analyzer.configure(sample_rate: 16_000)
      analyzer.buffer!(Array.new(analyzer.block_size, 0.1).pack("f*"))
      analyzer.analyze if analyze
    end

    3.times { exercise.call(analyze: true) }
    GC.start
    baseline = rss_mb

    100.times do |i|
      exercise.call(analyze: (i % 20).zero?)
    end
    GC.start
    growth = rss_mb - baseline

    # 100 discarded analyzers alone hold ~800MB native if their free paths
    # leak; a healthy run stays within noise of the baseline.
    expect(growth).to be < 100, "RSS grew #{growth.round(1)}MB over 100 lifecycle churns — native handles are leaking"
  end

  it "survives handle lifecycles under GC.stress with compaction" do
    model = Aicoustics::Model.from_file(SpecSupport.vad_model_path)

    GC.stress = true
    begin
      3.times do
        vad = Aicoustics::Vad.create(model, license)
        vad.configure(sample_rate: 16_000)
        vad.process!(Array.new(vad.block_size, 0.1).pack("f*"))
        expect([true, false]).to include(vad.context.speech_detected?)
      end
    ensure
      GC.stress = false
    end

    GC.compact
    vad = Aicoustics::Vad.create(model, license)
    vad.configure(sample_rate: 16_000)
    vad.process!(Array.new(vad.block_size, 0.1).pack("f*"))
    expect([true, false]).to include(vad.context.speech_detected?)
  end

  it "runs the production threading shape without crashes or lock-ups" do
    enhancer_model = Aicoustics::Model.from_file(SpecSupport.model_path)
    tyto_model = Aicoustics::Model.from_file(SpecSupport.analyzer_model_path)
    create_mutex = Mutex.new

    # Mirrors production: one processor per stream lane, one analyzer per
    # worker thread, all sharing model data through the SDK's internal
    # refcounting, with GC compaction fired mid-flight from another thread.
    workers = 4.times.map do |lane|
      Thread.new do
        processor = create_mutex.synchronize { Aicoustics::Processor.create(enhancer_model, license) }
        processor.configure(sample_rate: 16_000)
        analyzer = create_mutex.synchronize { Aicoustics::Analyzer.create(tyto_model, license) }
        analyzer.configure(sample_rate: 16_000)

        block = Array.new(processor.block_size, 0.1).pack("f*")
        tyto_block = Array.new(analyzer.block_size, 0.1).pack("f*")
        50.times do |i|
          processor.process!(block.dup)
          analyzer.buffer!(tyto_block)
          analyzer.analyze if (i % 25).zero?
        end
        :done
      end
    end
    compactor = Thread.new { 5.times { GC.compact; sleep 0.05 } }

    expect(workers.map(&:value)).to all(eq(:done))
    compactor.join
  end
end
