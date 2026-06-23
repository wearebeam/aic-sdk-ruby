# frozen_string_literal: true

require "mkmf"
require "rbconfig"

# Resolve the vendored SDK that ships in the repo/gem. Mirrors the slug logic in
# lib/aicoustics/native.rb so the C extension links the same artifact the FFI path loads.
require_relative "../../lib/aicoustics/version"

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

version     = ENV.fetch("AIC_SDK_VERSION", Aicoustics::SDK_VERSION)
slug        = "#{arch_token}-#{os_token}"
vendor_root = File.expand_path("../../vendor/aic", __dir__)
include_dir = File.join(vendor_root, "include")
lib_dir     = File.join(vendor_root, version, slug)

abort("aicoustics: header not found at #{include_dir}/aic.h — run `rake vendor:fetch`") \
  unless File.exist?(File.join(include_dir, "aic.h"))
abort("aicoustics: libaic not found in #{lib_dir} — run `rake vendor:fetch`") \
  unless Dir.exist?(lib_dir)

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
if os_token == "darwin"
  $LDFLAGS << " -Wl,-rpath,@loader_path/#{rel}"
else
  # $ORIGIN must reach the linker literally: escape $ for make, quote for the shell.
  $LDFLAGS << " -Wl,-rpath,'$$ORIGIN/#{rel}'"
end

# Absolute rpath to the build-time vendored dir as a dev fallback (harmless in a shipped gem;
# simply won't resolve on another machine, where the relative rpath above takes over).
$LDFLAGS << " -Wl,-rpath,#{lib_dir}"

create_makefile("aicoustics_ext")
