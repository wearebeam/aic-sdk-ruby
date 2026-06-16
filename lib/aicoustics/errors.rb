# frozen_string_literal: true

module Aicoustics
  class Error < StandardError
    attr_reader :code

    def initialize(message = nil, code: nil)
      @code = code
      super(message)
    end
  end

  class UnsupportedPlatformError < Error; end
  class LibraryLoadError < Error; end

  class NullPointerError < Error; end
  class ParameterOutOfRangeError < Error; end
  class NotInitializedError < Error; end
  class AudioConfigUnsupportedError < Error; end
  class AudioConfigMismatchError < Error; end
  class EnhancementNotAllowedError < Error; end
  class InternalError < Error; end

  class LicenseError < Error; end
  class LicenseFormatInvalidError < LicenseError; end
  class LicenseVersionUnsupportedError < LicenseError; end
  class LicenseExpiredError < LicenseError; end
  class TokenUpdateUnsupportedError < LicenseError; end

  class ModelError < Error; end
  class ModelInvalidError < ModelError; end
  class ModelVersionUnsupportedError < ModelError; end
  class ModelFilePathInvalidError < ModelError; end
  class FileSystemError < ModelError; end
  class ModelDataUnalignedError < ModelError; end
  class ModelTypeUnsupportedError < ModelError; end

  ERROR_TABLE = {
    null_pointer: [NullPointerError, "Required pointer argument was NULL"],
    parameter_out_of_range: [ParameterOutOfRangeError, "Parameter value is outside the acceptable range"],
    processor_not_initialized: [NotInitializedError, "Processor must be initialized before this operation; call #configure first"],
    audio_config_unsupported: [AudioConfigUnsupportedError, "Audio configuration (sample rate, channels, frames) is not supported by the model"],
    audio_config_mismatch: [AudioConfigMismatchError, "Audio buffer configuration differs from the one provided during initialization"],
    enhancement_not_allowed: [EnhancementNotAllowedError, "SDK key was not authorized or usage could not be reported; check the license tier and network egress to ai-coustics"],
    internal_error: [InternalError, "Internal SDK error; contact ai-coustics support"],
    license_format_invalid: [LicenseFormatInvalidError, "License key format is invalid or corrupted"],
    license_version_unsupported: [LicenseVersionUnsupportedError, "License version is not compatible with this SDK version"],
    license_expired: [LicenseExpiredError, "License key has expired"],
    token_update_unsupported: [TokenUpdateUnsupportedError, "In-place token update requires both keys to be JWT-form licenses"],
    model_invalid: [ModelInvalidError, "Model file is invalid or corrupted"],
    model_version_unsupported: [ModelVersionUnsupportedError, "Model file version is not compatible with this SDK version"],
    model_file_path_invalid: [ModelFilePathInvalidError, "Path to the model file is invalid"],
    file_system_error: [FileSystemError, "Model file could not be opened; verify that it exists"],
    model_data_unaligned: [ModelDataUnalignedError, "Model data is not aligned to 64 bytes"],
    model_type_unsupported: [ModelTypeUnsupportedError, "Model type is not supported by this processor"]
  }.freeze

  module_function

  def success?(code)
    code == :success || code == 0
  end

  def check!(code)
    return code if success?(code)

    klass, message = ERROR_TABLE[code]
    raise klass.new(message, code: code) if klass

    raise Error.new("ai-coustics SDK returned an unknown error code: #{code.inspect}", code: code)
  end
end
