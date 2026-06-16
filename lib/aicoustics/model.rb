# frozen_string_literal: true

module Aicoustics
  class Model
    MODEL_ALIGNMENT = 64

    attr_reader :handle

    def self.from_file(path)
      out = FFI::MemoryPointer.new(:pointer)
      Aicoustics.check!(Native.aic_model_create_from_file(out, path.to_s))
      new(out.read_pointer)
    end

    def self.from_buffer(bytes)
      bytes = bytes.b
      backing = FFI::MemoryPointer.new(:uint8, bytes.bytesize + MODEL_ALIGNMENT)
      offset = (MODEL_ALIGNMENT - (backing.address % MODEL_ALIGNMENT)) % MODEL_ALIGNMENT
      aligned = backing + offset
      aligned.put_bytes(0, bytes)

      out = FFI::MemoryPointer.new(:pointer)
      Aicoustics.check!(Native.aic_model_create_from_buffer(out, aligned, bytes.bytesize))
      model = new(out.read_pointer)
      model.instance_variable_set(:@backing_buffer, backing)
      model
    end

    def initialize(pointer)
      @handle = FFI::AutoPointer.new(pointer, Native.method(:aic_model_destroy))
    end

    def id
      Native.aic_model_get_id(@handle)
    end

    def optimal_sample_rate
      out = FFI::MemoryPointer.new(:uint32)
      Aicoustics.check!(Native.aic_model_get_optimal_sample_rate(@handle, out))
      out.read_uint32
    end

    def optimal_num_frames(sample_rate = optimal_sample_rate)
      out = FFI::MemoryPointer.new(:size_t)
      Aicoustics.check!(Native.aic_model_get_optimal_num_frames(@handle, sample_rate, out))
      out.read(:size_t)
    end
  end
end
