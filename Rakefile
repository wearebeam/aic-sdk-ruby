# frozen_string_literal: true

require "rspec/core/rake_task"

RSpec::Core::RakeTask.new(:spec)
task default: :spec

PLATFORMS = {
  "aarch64-darwin" => { asset: "aarch64-apple-darwin", lib: "libaic.dylib" },
  "x86_64-darwin" => { asset: "x86_64-apple-darwin", lib: "libaic.dylib" },
  "aarch64-linux" => { asset: "aarch64-unknown-linux-gnu", lib: "libaic.so" },
  "x86_64-linux" => { asset: "x86_64-unknown-linux-gnu", lib: "libaic.so" }
}.freeze

namespace :vendor do
  desc "Download and vendor the native libaic libraries for all platforms (VERSION=0.20.0)"
  task :fetch do
    require_relative "lib/aicoustics/version"
    version = ENV.fetch("VERSION", Aicoustics::SDK_VERSION)
    repo = "ai-coustics/aic-sdk-c"

    require "tmpdir"
    require "fileutils"

    PLATFORMS.each do |slug, info|
      asset = "aic-sdk-#{info[:asset]}-#{version}.tar.gz"
      dest_dir = File.join(__dir__, "vendor", "aic", version, slug)
      FileUtils.mkdir_p(dest_dir)
      FileUtils.mkdir_p(File.join(__dir__, "vendor", "aic", "include"))

      Dir.mktmpdir do |tmp|
        sh "gh release download #{version} -R #{repo} -p #{asset} -D #{tmp} --clobber"
        sh "tar xzf #{File.join(tmp, asset)} -C #{tmp}"
        FileUtils.cp(File.join(tmp, "lib", info[:lib]), File.join(dest_dir, info[:lib]))
        header_src = File.join(tmp, "include", "aic.h")
        FileUtils.cp(header_src, File.join(__dir__, "vendor", "aic", "include", "aic.h")) if File.exist?(header_src)
      end
      puts "vendored #{slug}/#{info[:lib]}"
    end
  end
end

namespace :model do
  desc "Download an .aicmodel for local development/specs (URL=... [DEST=spec/fixtures/model.aicmodel])"
  task :fetch do
    url = ENV.fetch("URL")
    dest = ENV.fetch("DEST", File.join(__dir__, "spec", "fixtures", "model.aicmodel"))
    require "fileutils"
    FileUtils.mkdir_p(File.dirname(dest))
    sh "curl -fL -o #{dest} #{url}"
    puts "model saved to #{dest}"
  end
end
