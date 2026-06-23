# frozen_string_literal: true

require "mkmf"
require "rbconfig"

# The proprietary libaic binary is license-restricted: it is never committed or
# packaged into the gem. Fetch it (header + lib) from the public ai-coustics
# release at install time when it isn't already vendored. AicSdk owns the asset
# names, layout, and checksums; lib/aicoustics/native.rb mirrors the same slug
# logic so the runtime loader finds the artifact this links against.
require_relative "aic_sdk"

version     = ENV.fetch("AIC_SDK_VERSION", Aicoustics::SDK_VERSION)
slug        = Aicoustics::AicSdk.current_slug

# Downloads + verifies the SHA256 only if the artifact is missing (no-op for a
# checkout primed by `rake vendor:fetch` or a restored CI cache).
Aicoustics::AicSdk.ensure!(version: version, slug: slug)

include_dir = Aicoustics::AicSdk.include_dir
lib_dir     = Aicoustics::AicSdk.lib_dir(version, slug)

# Header + link line. find_library both adds -L/-laic and proves the symbol resolves.
find_header("aic.h", include_dir) || abort("aicoustics: cannot use aic.h")
find_library("aic", "aic_get_sdk_version", lib_dir) || abort("aicoustics: cannot link libaic")

# Runtime lookup: libaic's install name is @rpath/libaic.dylib (and the .so is unqualified),
# so the extension needs an rpath that resolves to the vendored lib dir.
#
# The compiled ext installs at lib/aicoustics/aicoustics_ext.{bundle,so}; the vendored libs
# sit at vendor/aic/<version>/<slug>/ — i.e. ../../vendor/aic/<version>/<slug> relative to the
# ext's directory. A loader-relative rpath keeps the gem relocatable (works wherever installed).
rel = "../../vendor/aic/#{version}/#{slug}"
if Aicoustics::AicSdk.os_token == "darwin"
  $LDFLAGS << " -Wl,-rpath,@loader_path/#{rel}"
else
  # $ORIGIN must reach the linker literally: escape $ for make, quote for the shell.
  $LDFLAGS << " -Wl,-rpath,'$$ORIGIN/#{rel}'"
end

# Absolute rpath to the build-time vendored dir as a dev fallback (harmless in a shipped gem;
# simply won't resolve on another machine, where the relative rpath above takes over).
$LDFLAGS << " -Wl,-rpath,#{lib_dir}"

create_makefile("aicoustics_ext")
