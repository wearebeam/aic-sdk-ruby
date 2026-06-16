# frozen_string_literal: true

require_relative "lib/aicoustics/version"

Gem::Specification.new do |spec|
  spec.name = "aicoustics"
  spec.version = Aicoustics::VERSION
  spec.authors = ["jjholmes927"]

  spec.summary = "Ruby FFI bindings for the ai-coustics speech enhancement SDK (v#{Aicoustics::SDK_VERSION})"
  spec.description = "Server-side Ruby bindings for the ai-coustics SDK: speech enhancement, " \
    "voice activity detection, and the Tyto audio-quality analyzer, over the vendored native library."
  spec.homepage = "https://github.com/jjholmes927/aic-sdk-ruby"
  spec.license = "Apache-2.0"
  spec.required_ruby_version = ">= 3.1"

  spec.metadata = {
    "allowed_push_host" => "https://gems.internal.invalid",
    "rubygems_mfa_required" => "true",
    "source_code_uri" => spec.homepage
  }

  spec.files = Dir.glob("lib/**/*.rb") +
    Dir.glob("vendor/**/*").select { |path| File.file?(path) } +
    %w[README.md LICENSE aicoustics.gemspec]
  spec.require_paths = ["lib"]

  spec.add_dependency "ffi", ">= 1.15"
end
