# frozen_string_literal: true

require_relative "aicoustics/version"
require_relative "aicoustics/errors"
require_relative "aicoustics/native"
# C extension: Model, Processor, contexts, Analyzer, sdk_version.
# Dev/rake-compiler builds it into lib/aicoustics/; `gem install` builds it onto the
# extension load path as a top-level file — try both.
begin
  require_relative "aicoustics/aicoustics_ext"
rescue LoadError
  require "aicoustics_ext"
end
require_relative "aicoustics/analysis_result"
require_relative "aicoustics/otel_config"
require_relative "aicoustics/processor_context"
require_relative "aicoustics/vad_context"
require_relative "aicoustics/pcm"
require_relative "aicoustics/pipeline"

module Aicoustics
  module_function

  # #sdk_version and #compatible_model_version are defined by the C extension.

  def library_path
    Native.library_path
  end

  def enhance_pcm(pcm_s16le, **options)
    Pipeline.enhance_pcm(pcm_s16le, **options)
  end
end

Aicoustics._set_wrapper_id("aicoustics-ruby/#{Aicoustics::VERSION}") if Aicoustics.respond_to?(:_set_wrapper_id)
