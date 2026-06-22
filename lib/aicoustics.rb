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

# NB: the SDK's aic_set_sdk_wrapper_id(uint32_t) is intentionally not called. It tags
# telemetry with an ai-coustics-assigned per-wrapper ID (C++=1, Rust=2); there is no Ruby
# ID, and the official Python and Node bindings likewise don't call it. (The old FFI binding
# called it with a String, which mismatched the uint32_t ABI.)
