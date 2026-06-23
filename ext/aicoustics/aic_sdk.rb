# frozen_string_literal: true

require "rbconfig"
require "net/http"
require "uri"
require "digest"
require "fileutils"
require "tmpdir"

require_relative "../../lib/aicoustics/version"

module Aicoustics
  # Locates, downloads, and verifies the proprietary libaic native artifact.
  #
  # The artifact is published in the *public* ai-coustics/aic-sdk-c GitHub
  # releases and is freely downloadable, but its license forbids redistribution
  # — so it is never committed to this repo nor packaged into the published gem.
  # Instead it is fetched at build time (extconf.rb) or on demand
  # (`rake vendor:fetch`). This module is the single source of truth for asset
  # names, on-disk layout, and checksums, shared by both callers.
  module AicSdk
    module_function

    REPO = "ai-coustics/aic-sdk-c"

    # platform slug => { asset: <release-asset infix>, lib: <library filename> }
    PLATFORMS = {
      "aarch64-darwin" => { asset: "aarch64-apple-darwin", lib: "libaic.dylib" },
      "x86_64-darwin" => { asset: "x86_64-apple-darwin", lib: "libaic.dylib" },
      "aarch64-linux" => { asset: "aarch64-unknown-linux-gnu", lib: "libaic.so" },
      "x86_64-linux" => { asset: "x86_64-unknown-linux-gnu", lib: "libaic.so" }
    }.freeze

    # SHA256 of each release tarball, keyed by SDK version then platform slug.
    # Refresh when bumping SDK_VERSION: `rake vendor:checksums VERSION=x.y.z`.
    CHECKSUMS = {
      "0.20.0" => {
        "aarch64-darwin" => "3e156d724e166b1d95b5df70227164759215670d1a6dec68a8b6f9e009914102",
        "x86_64-darwin" => "a58ce9458fcf09265d739494457f4b0473dc5a66ec2f967d7962265ff8d492e7",
        "aarch64-linux" => "fbbc8bac41712981f379acfad8408afeb43f6c95b101bb3e81067bd0e604d86c",
        "x86_64-linux" => "05f3829b9041e370c1edba2ed9fc481b866e18d2658a37e6d93e66ea2d719f7a"
      }
    }.freeze

    # --- platform resolution (mirrors lib/aicoustics/native.rb) ---------------

    def current_slug
      "#{arch_token}-#{os_token}"
    end

    def arch_token
      case RbConfig::CONFIG["host_cpu"]
      when /arm64|aarch64/ then "aarch64"
      when /x86_64|x64|amd64/ then "x86_64"
      else abort("aicoustics: unsupported CPU architecture #{RbConfig::CONFIG["host_cpu"].inspect}")
      end
    end

    def os_token
      case RbConfig::CONFIG["host_os"]
      when /darwin/ then "darwin"
      when /linux/ then "linux"
      else abort("aicoustics: unsupported operating system #{RbConfig::CONFIG["host_os"].inspect}")
      end
    end

    # --- on-disk layout -------------------------------------------------------

    def vendor_root
      File.expand_path("../../vendor/aic", __dir__)
    end

    def include_dir
      File.join(vendor_root, "include")
    end

    def lib_dir(version, slug)
      File.join(vendor_root, version, slug)
    end

    def asset_url(version, slug)
      "https://github.com/#{REPO}/releases/download/#{version}/" \
        "aic-sdk-#{PLATFORMS.fetch(slug)[:asset]}-#{version}.tar.gz"
    end

    # --- public entry points --------------------------------------------------

    # Ensure the header + libaic for `slug` are present, fetching them only if
    # missing. Idempotent: a developer who ran `rake vendor:fetch` (or a CI cache
    # that restored vendor/) skips the network entirely.
    def ensure!(version: Aicoustics::SDK_VERSION, slug: current_slug)
      info = PLATFORMS.fetch(slug) { abort("aicoustics: unsupported platform #{slug.inspect}") }
      lib = File.join(lib_dir(version, slug), info[:lib])
      header = File.join(include_dir, "aic.h")
      return if File.exist?(lib) && File.exist?(header)

      if ENV["AIC_SDK_OFFLINE"]
        abort("aicoustics: libaic missing for #{slug} and AIC_SDK_OFFLINE is set.\n" \
              "Run `rake vendor:fetch` (or restore vendor/aic) before building offline.")
      end

      fetch_platform(version, slug)
    end

    # Download, verify, and install the SDK for one platform, overwriting any
    # existing copy. Used by `rake vendor:fetch` and as ensure!'s fallback.
    def fetch_platform(version, slug)
      info = PLATFORMS.fetch(slug) { abort("aicoustics: unsupported platform #{slug.inspect}") }
      expected = checksum_for(version, slug)

      Dir.mktmpdir do |tmp|
        tarball = File.join(tmp, "aic-sdk.tar.gz")

        local = ENV["AIC_SDK_TARBALL"]
        if local && !local.empty?
          abort("aicoustics: AIC_SDK_TARBALL #{local.inspect} not found") unless File.exist?(local)
          FileUtils.cp(local, tarball)
        else
          download(asset_url(version, slug), tarball)
        end

        actual = Digest::SHA256.file(tarball).hexdigest
        unless actual == expected
          abort("aicoustics: checksum mismatch for #{slug} (#{version})\n" \
                "  expected #{expected}\n  actual   #{actual}")
        end

        system("tar", "xzf", tarball, "-C", tmp) || abort("aicoustics: failed to extract #{tarball}")

        FileUtils.mkdir_p(lib_dir(version, slug))
        FileUtils.mkdir_p(include_dir)
        FileUtils.cp(File.join(tmp, "lib", info[:lib]), File.join(lib_dir(version, slug), info[:lib]))
        FileUtils.cp(File.join(tmp, "include", "aic.h"), File.join(include_dir, "aic.h"))
      end

      "#{slug}/#{info[:lib]}"
    end

    # Download every supported platform (developer convenience).
    def fetch_all(version: Aicoustics::SDK_VERSION)
      PLATFORMS.keys.map { |slug| fetch_platform(version, slug) }
    end

    def checksum_for(version, slug)
      sums = CHECKSUMS.fetch(version) do
        abort("aicoustics: no checksums pinned for SDK #{version} — " \
              "run `rake vendor:checksums VERSION=#{version}` and add them to #{__FILE__}")
      end
      sums.fetch(slug) { abort("aicoustics: no checksum for #{slug} at #{version}") }
    end

    # Stream a URL to `dest`, following redirects (GitHub releases redirect to a
    # CDN). Net::HTTP keeps this dependency-free — no gh/curl at install time.
    def download(url, dest, limit = 5)
      abort("aicoustics: too many redirects fetching #{url}") if limit <= 0
      uri = URI(url)
      Net::HTTP.start(uri.host, uri.port, use_ssl: uri.scheme == "https") do |http|
        http.request(Net::HTTP::Get.new(uri)) do |res|
          case res
          when Net::HTTPSuccess
            File.open(dest, "wb") { |f| res.read_body { |chunk| f.write(chunk) } }
          when Net::HTTPRedirection
            return download(res["location"], dest, limit - 1)
          else
            abort("aicoustics: download failed (HTTP #{res.code}) for #{url}")
          end
        end
      end
    end
  end
end
