# frozen_string_literal: true

require "ffi"
require "rbconfig"

module Aicoustics
  module Native
    extend FFI::Library

    class << self
      def platform_slug
        "#{arch_token}-#{os_token}"
      end

      def library_filename
        os_token == "darwin" ? "libaic.dylib" : "libaic.so"
      end

      def default_library_path
        File.join(vendor_root, Aicoustics::SDK_VERSION, platform_slug, library_filename)
      end

      def library_path
        override = ENV["AIC_SDK_LIB_PATH"]
        return override if override && !override.empty?

        default_library_path
      end

      private

      def vendor_root
        File.expand_path("../../vendor/aic", __dir__)
      end

      def arch_token
        case RbConfig::CONFIG["host_cpu"]
        when /arm64|aarch64/ then "aarch64"
        when /x86_64|x64|amd64/ then "x86_64"
        else
          raise UnsupportedPlatformError, "unsupported CPU architecture: #{RbConfig::CONFIG["host_cpu"]}"
        end
      end

      def os_token
        case RbConfig::CONFIG["host_os"]
        when /darwin/ then "darwin"
        when /linux/ then "linux"
        else
          raise UnsupportedPlatformError, "unsupported operating system: #{RbConfig::CONFIG["host_os"]}"
        end
      end
    end

    resolved_path = library_path
    unless File.exist?(resolved_path)
      raise LibraryLoadError, "ai-coustics native library not found at #{resolved_path.inspect}. " \
        "Vendor it for #{platform_slug} or set AIC_SDK_LIB_PATH."
    end

    begin
      ffi_lib resolved_path
    rescue LoadError => e
      raise LibraryLoadError, "failed to load ai-coustics native library at #{resolved_path.inspect}: #{e.message}"
    end

    enum :error_code, [
      :success, 0,
      :null_pointer, 1,
      :parameter_out_of_range, 2,
      :processor_not_initialized, 3,
      :audio_config_unsupported, 4,
      :audio_config_mismatch, 5,
      :enhancement_not_allowed, 6,
      :internal_error, 7,
      :license_format_invalid, 50,
      :license_version_unsupported, 51,
      :license_expired, 52,
      :token_update_unsupported, 53,
      :model_invalid, 100,
      :model_version_unsupported, 101,
      :model_file_path_invalid, 102,
      :file_system_error, 103,
      :model_data_unaligned, 104,
      :model_type_unsupported, 105
    ]

    enum :processor_parameter, [
      :bypass, 0,
      :enhancement_level, 1
    ]

    enum :vad_parameter, [
      :speech_hold_duration, 0,
      :sensitivity, 1,
      :minimum_speech_duration, 2
    ]

    class OtelConfig < FFI::Struct
      layout :enable, :bool,
        :session_id, :pointer,
        :export_interval_ms, :uint32
    end

    class AnalysisResult < FFI::Struct
      layout :risk_score, :float,
        :speaker_reverb, :float,
        :speaker_loudness, :float,
        :interfering_speech, :float,
        :media_speech, :float,
        :noise, :float,
        :packet_loss, :float
    end

    attach_function :aic_get_sdk_version, [], :string
    attach_function :aic_get_compatible_model_version, [], :uint32

    attach_function :aic_model_create_from_file, [:pointer, :string], :error_code
    attach_function :aic_model_create_from_buffer, [:pointer, :pointer, :size_t], :error_code
    attach_function :aic_model_destroy, [:pointer], :void
    attach_function :aic_model_get_id, [:pointer], :string
    attach_function :aic_model_get_optimal_sample_rate, [:pointer, :pointer], :error_code
    attach_function :aic_model_get_optimal_num_frames, [:pointer, :uint32, :pointer], :error_code

    attach_function :aic_processor_create, [:pointer, :pointer, :string, :pointer], :error_code
    attach_function :aic_processor_destroy, [:pointer], :void
    attach_function :aic_processor_initialize, [:pointer, :uint32, :uint16, :size_t, :bool], :error_code
    attach_function :aic_processor_process_planar, [:pointer, :pointer, :uint16, :size_t], :error_code
    attach_function :aic_processor_process_interleaved, [:pointer, :pointer, :uint16, :size_t], :error_code
    attach_function :aic_processor_process_sequential, [:pointer, :pointer, :uint16, :size_t], :error_code

    attach_function :aic_processor_context_create, [:pointer, :pointer], :error_code
    attach_function :aic_processor_context_destroy, [:pointer], :void
    attach_function :aic_processor_context_reset, [:pointer], :error_code
    attach_function :aic_processor_context_set_parameter, [:pointer, :processor_parameter, :float], :error_code
    attach_function :aic_processor_context_get_parameter, [:pointer, :processor_parameter, :pointer], :error_code
    attach_function :aic_processor_context_get_output_delay, [:pointer, :pointer], :error_code
    attach_function :aic_processor_context_update_bearer_token, [:pointer, :string], :error_code

    attach_function :aic_vad_context_create, [:pointer, :pointer], :error_code
    attach_function :aic_vad_context_destroy, [:pointer], :void
    attach_function :aic_vad_context_is_speech_detected, [:pointer, :pointer], :error_code
    attach_function :aic_vad_context_set_parameter, [:pointer, :vad_parameter, :float], :error_code
    attach_function :aic_vad_context_get_parameter, [:pointer, :vad_parameter, :pointer], :error_code

    attach_function :aic_analyzer_pair_create, [:pointer, :pointer, :pointer, :string], :error_code
    attach_function :aic_collector_initialize, [:pointer, :uint32, :uint16, :size_t, :bool], :error_code
    attach_function :aic_collector_buffer_planar, [:pointer, :pointer, :uint16, :size_t], :error_code
    attach_function :aic_collector_buffer_interleaved, [:pointer, :pointer, :uint16, :size_t], :error_code
    attach_function :aic_collector_buffer_sequential, [:pointer, :pointer, :uint16, :size_t], :error_code
    attach_function :aic_collector_destroy, [:pointer], :void
    attach_function :aic_analyzer_reset, [:pointer], :error_code
    attach_function :aic_analyzer_analyze_buffered, [:pointer, :pointer], :error_code
    attach_function :aic_analyzer_update_bearer_token, [:pointer, :string], :error_code
    attach_function :aic_analyzer_destroy, [:pointer], :void

    @wrapper_id_supported =
      begin
        attach_function :aic_set_sdk_wrapper_id, [:string], :void
        true
      rescue FFI::NotFoundError
        false
      end

    def self.wrapper_id_supported?
      @wrapper_id_supported
    end
  end
end
