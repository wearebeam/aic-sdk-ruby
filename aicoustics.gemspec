# frozen_string_literal: true

require_relative "lib/aicoustics/version"

Gem::Specification.new do |spec|
  spec.name = "aicoustics"
  spec.version = Aicoustics::VERSION
  spec.authors = ["wearebeam"]

  spec.summary = "Native C bindings for the ai-coustics speech enhancement SDK (v#{Aicoustics::SDK_VERSION})"
  spec.description = "Server-side Ruby bindings for the ai-coustics SDK: speech enhancement, " \
    "voice activity detection, and the Tyto audio-quality analyzer, as a native C extension " \
    "over the vendored native library."
  spec.homepage = "https://github.com/wearebeam/aic-sdk-ruby"
  spec.license = "Apache-2.0"
  spec.required_ruby_version = ">= 3.1"

  spec.metadata = {
    "allowed_push_host" => "https://gems.internal.invalid",
    "rubygems_mfa_required" => "true",
    "source_code_uri" => spec.homepage
  }

  # The proprietary libaic binary is license-restricted and MUST NOT be packaged:
  # it is fetched from the public ai-coustics release at install time by
  # ext/aicoustics/extconf.rb. The gem ships only the Apache-2.0 wrapper + the C
  # extension sources, which compile against the fetched header and lib.
  spec.files = Dir.glob("lib/**/*.rb") +
    Dir.glob("ext/**/*.{c,h,rb}") +
    %w[README.md LICENSE LICENSE.AIC-SDK NOTICE aicoustics.gemspec]
  spec.require_paths = ["lib"]
  spec.extensions = ["ext/aicoustics/extconf.rb"]

  spec.add_development_dependency "rake-compiler", "~> 1.2"
end
