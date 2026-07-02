# frozen_string_literal: true

require "mkmf"
require "rbconfig"

# The proprietary libaic binary is license-restricted: it is never committed or
# packaged into the gem. Fetch it (header + lib) from the public ai-coustics
# release at install time when it isn't already vendored. SdkFetcher owns the
# asset names, layout, and checksums; lib/aicoustics/native.rb mirrors the same
# slug logic so the runtime loader finds the artifact this links against.
require_relative "sdk_fetcher"

version     = ENV.fetch("AIC_SDK_VERSION", Aicoustics::SDK_VERSION)
slug        = Aicoustics::SdkFetcher.current_slug

# Downloads + verifies the SHA256 only if the artifact is missing (no-op for a
# checkout primed by `rake vendor:fetch` or a restored CI cache).
Aicoustics::SdkFetcher.ensure!(version: version, slug: slug)

include_dir = Aicoustics::SdkFetcher.include_dir
lib_dir     = Aicoustics::SdkFetcher.lib_dir(version, slug)

# Header + link line. find_library both adds -L/-laic and proves the symbol resolves.
find_header("aic.h", include_dir) || abort("aicoustics: cannot use aic.h")
find_library("aic", "aic_get_sdk_version", lib_dir) || abort("aicoustics: cannot link libaic")

# Runtime lookup: libaic's install name is @rpath/libaic.dylib (and the .so is unqualified),
# so the extension needs an rpath that resolves to the vendored lib dir.
#
# The compiled ext lands at a different depth depending on how it was built, and the
# vendored libs always sit at <gem>/vendor/aic/<version>/<slug>/:
#   - `gem install` / bundle:   lib/aicoustics_ext.{bundle,so}           -> ../vendor/aic
#   - rake-compiler dev build:  lib/aicoustics/aicoustics_ext.{bundle,so} -> ../../vendor/aic
# Bake a loader-relative rpath for BOTH depths so the gem is genuinely relocatable in either
# layout. (Previously only ../../ was baked, so installed gems silently relied on the absolute
# rpath below — which breaks on any deploy where the build dir differs from the run dir, e.g.
# Heroku's ephemeral /tmp/build_*.)
rels = ["../vendor/aic/#{version}/#{slug}", "../../vendor/aic/#{version}/#{slug}"]
rels.each do |rel|
  if Aicoustics::SdkFetcher.os_token == "darwin"
    $LDFLAGS << " -Wl,-rpath,@loader_path/#{rel}"
  else
    # $ORIGIN must reach the linker literally: escape $ for make, quote for the shell.
    $LDFLAGS << " -Wl,-rpath,'$$ORIGIN/#{rel}'"
  end
end

# Absolute rpath to the build-time vendored dir as a dev fallback (harmless in a shipped gem;
# simply won't resolve on another machine, where the relative rpath above takes over).
$LDFLAGS << " -Wl,-rpath,#{lib_dir}"

create_makefile("aicoustics_ext")
