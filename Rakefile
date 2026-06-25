# frozen_string_literal: true

require "rspec/core/rake_task"
require "rake/extensiontask"

Rake::ExtensionTask.new("aicoustics_ext") do |ext|
  ext.ext_dir = "ext/aicoustics"
  ext.lib_dir = "lib/aicoustics" # dev build lands at lib/aicoustics/aicoustics_ext.<ext>
end

RSpec::Core::RakeTask.new(:spec)
task spec: :compile
task default: :spec

namespace :vendor do
  desc "Download and vendor the native libaic libraries for all platforms (VERSION=0.20.0)"
  task :fetch do
    require_relative "ext/aicoustics/sdk_fetcher"
    version = ENV.fetch("VERSION", Aicoustics::SDK_VERSION)
    Aicoustics::SdkFetcher.fetch_all(version: version).each { |what| puts "vendored #{what}" }
  end

  desc "Print SHA256 checksums for every platform tarball (VERSION=0.20.0) to pin in sdk_fetcher.rb"
  task :checksums do
    require_relative "ext/aicoustics/sdk_fetcher"
    require "digest"
    require "tmpdir"
    version = ENV.fetch("VERSION", Aicoustics::SDK_VERSION)
    puts %(    "#{version}" => {)
    Aicoustics::SdkFetcher::PLATFORMS.each_key do |slug|
      Dir.mktmpdir do |tmp|
        tarball = File.join(tmp, "aic-sdk.tar.gz")
        Aicoustics::SdkFetcher.download(Aicoustics::SdkFetcher.asset_url(version, slug), tarball)
        puts %(      "#{slug}" => "#{Digest::SHA256.file(tarball).hexdigest}",)
      end
    end
    puts "    },"
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
