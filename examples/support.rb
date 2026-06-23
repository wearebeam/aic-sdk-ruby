# frozen_string_literal: true

require "rbconfig"
require "fileutils"

# Plumbing for the examples (.env, WAV<->PCM, playback) so the demos themselves
# can stay focused on the ai-coustics SDK.
module Support
  module_function

  # Load KEY=VALUE pairs from <dir>/.env into ENV (without overriding existing vars).
  def load_env(dir)
    path = File.join(dir, ".env")
    return unless File.exist?(path)
    File.foreach(path) do |line|
      line = line.strip
      next if line.empty? || line.start_with?("#")
      key, _, value = line.partition("=")
      ENV[key.strip] ||= value.strip.gsub(/\A["']|["']\z/, "") unless key.empty?
    end
  end

  # filename -> [mono 16-bit little-endian PCM (String), sample_rate]. Stereo is
  # downmixed; aborts on anything that isn't 16-bit PCM WAV.
  def read_wav_mono(path)
    bytes = File.binread(path)
    abort "#{path}: not a WAVE file" unless bytes.byteslice(0, 4) == "RIFF" && bytes.byteslice(8, 4) == "WAVE"

    fmt = nil
    data = nil
    pos = 12
    while pos + 8 <= bytes.bytesize
      id = bytes.byteslice(pos, 4)
      size = bytes.byteslice(pos + 4, 4).unpack1("V")
      body = bytes.byteslice(pos + 8, size)
      if id == "fmt "
        audio_format, channels, sample_rate, _byte_rate, _block_align, bits = body.unpack("vvVVvv")
        fmt = { audio_format: audio_format, channels: channels, sample_rate: sample_rate, bits: bits }
      elsif id == "data"
        data = body
      end
      pos += 8 + size + (size.odd? ? 1 : 0) # chunks are word-aligned
    end

    abort "#{path}: need 16-bit PCM (convert: ffmpeg -i in -ac 1 -ar 16000 -c:a pcm_s16le out.wav)" \
      unless fmt && data && fmt[:audio_format] == 1 && fmt[:bits] == 16

    pcm = fmt[:channels] > 1 ? data.unpack("s<*").each_slice(fmt[:channels]).map { |f| f.sum / fmt[:channels] }.pack("s<*") : data
    [pcm, fmt[:sample_rate]]
  end

  # Write mono 16-bit little-endian PCM out as a .wav (creates the directory).
  def write_wav(path, pcm_s16le, sample_rate)
    FileUtils.mkdir_p(File.dirname(path))
    header = [
      "RIFF", 36 + pcm_s16le.bytesize, "WAVE",
      "fmt ", 16, 1, 1, sample_rate, sample_rate * 2, 2, 16,
      "data", pcm_s16le.bytesize
    ].pack("a4Va4 a4VvvVVvv a4V")
    File.binwrite(path, header + pcm_s16le)
  end

  # Play a .wav file (afplay on macOS, ffplay/aplay on Linux).
  def play(path)
    if RbConfig::CONFIG["host_os"] =~ /darwin/
      system("afplay", path)
    elsif system("command -v ffplay > /dev/null 2>&1")
      system("ffplay", "-autoexit", "-nodisp", "-loglevel", "quiet", path)
    else
      system("aplay", "-q", path)
    end
  end
end
