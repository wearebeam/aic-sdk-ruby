# frozen_string_literal: true

require_relative "aicoustics/version"
require_relative "aicoustics/errors"
require_relative "aicoustics/native"
require_relative "aicoustics/analysis_result"
require_relative "aicoustics/otel_config"
require_relative "aicoustics/model"
require_relative "aicoustics/processor"
require_relative "aicoustics/processor_context"
require_relative "aicoustics/vad_context"
require_relative "aicoustics/analyzer"
require_relative "aicoustics/pcm"
require_relative "aicoustics/pipeline"

module Aicoustics
  module_function

  def sdk_version
    Native.aic_get_sdk_version
  end

  def compatible_model_version
    Native.aic_get_compatible_model_version
  end

  def library_path
    Native.library_path
  end

  def enhance_pcm(pcm_s16le, **options)
    Pipeline.enhance_pcm(pcm_s16le, **options)
  end

  if Native.wrapper_id_supported?
    Native.aic_set_sdk_wrapper_id("aicoustics-ruby/#{VERSION}")
  end
end
