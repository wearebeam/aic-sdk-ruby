# frozen_string_literal: true

require "rbconfig"

module Aicoustics
  # Platform/path resolution for the vendored native library. The C extension
  # links libaic at build time and loads it via an rpath, so this no longer does
  # any FFI work — it remains for diagnostics (e.g. Aicoustics.library_path) and
  # to locate the artifact the extension was built against.
  module Native
    class << self
      def platform_slug
        "#{arch_token}-#{os_token}"
      end

      def library_filename
        os_token == "darwin" ? "libaic.dylib" : "libaic.so"
      end

      def library_path
        File.join(vendor_root, Aicoustics::SDK_VERSION, platform_slug, library_filename)
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
  end
end
