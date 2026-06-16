# frozen_string_literal: true

require "aicoustics"

module SpecSupport
  module_function

  def license_key
    ENV["AIC_SDK_LICENSE"]
  end

  def model_path
    ENV["AIC_SDK_MODEL"]
  end

  def analyzer_model_path
    ENV["AIC_SDK_ANALYZER_MODEL"]
  end

  def license?
    license_key && !license_key.empty?
  end

  def model?
    model_path && File.exist?(model_path.to_s)
  end

  def analyzer_model?
    analyzer_model_path && File.exist?(analyzer_model_path.to_s)
  end
end

RSpec.configure do |config|
  config.expect_with :rspec do |expectations|
    expectations.syntax = :expect
  end
  config.disable_monkey_patching!
  config.order = :random
end
