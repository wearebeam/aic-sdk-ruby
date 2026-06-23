#include "aicoustics.h"

/*
 * Entry point and shared infrastructure for the ai-coustics C extension:
 * the module/class handle storage, AicErrorCode -> exception mapping, the
 * config-ivar reader, and Init_aicoustics_ext which wires everything up.
 *
 * The handle types themselves live in model.c, processor.c, and analyzer.c.
 * Audio buffers cross the boundary as binary Strings of little-endian float32
 * samples (process_*! mutate in place) or as Arrays of Float (#process).
 *
 * Error handling routes through aic_check, which delegates to Aicoustics.check!
 * so all error policy stays in one place (lib/aicoustics/errors.rb).
 */

VALUE mAicoustics;
VALUE cModel, cProcessor, cProcessorContext, cVadContext, cAnalyzer;

ID id_check_bang, id_optimal_num_frames, id_from_h;
ID id_enable, id_session_id, id_export_interval_ms;

/* ---- error mapping -------------------------------------------------- */

static const char *code_to_sym(enum AicErrorCode code) {
  switch (code) {
    case AIC_ERROR_CODE_SUCCESS:                     return "success";
    case AIC_ERROR_CODE_NULL_POINTER:                return "null_pointer";
    case AIC_ERROR_CODE_PARAMETER_OUT_OF_RANGE:      return "parameter_out_of_range";
    case AIC_ERROR_CODE_PROCESSOR_NOT_INITIALIZED:   return "processor_not_initialized";
    case AIC_ERROR_CODE_AUDIO_CONFIG_UNSUPPORTED:    return "audio_config_unsupported";
    case AIC_ERROR_CODE_AUDIO_CONFIG_MISMATCH:       return "audio_config_mismatch";
    case AIC_ERROR_CODE_ENHANCEMENT_NOT_ALLOWED:     return "enhancement_not_allowed";
    case AIC_ERROR_CODE_INTERNAL_ERROR:              return "internal_error";
    case AIC_ERROR_CODE_LICENSE_FORMAT_INVALID:      return "license_format_invalid";
    case AIC_ERROR_CODE_LICENSE_VERSION_UNSUPPORTED: return "license_version_unsupported";
    case AIC_ERROR_CODE_LICENSE_EXPIRED:             return "license_expired";
    case AIC_ERROR_CODE_TOKEN_UPDATE_UNSUPPORTED:    return "token_update_unsupported";
    case AIC_ERROR_CODE_MODEL_INVALID:               return "model_invalid";
    case AIC_ERROR_CODE_MODEL_VERSION_UNSUPPORTED:   return "model_version_unsupported";
    case AIC_ERROR_CODE_MODEL_FILE_PATH_INVALID:     return "model_file_path_invalid";
    case AIC_ERROR_CODE_FILE_SYSTEM_ERROR:           return "file_system_error";
    case AIC_ERROR_CODE_MODEL_DATA_UNALIGNED:        return "model_data_unaligned";
    case AIC_ERROR_CODE_MODEL_TYPE_UNSUPPORTED:      return "model_type_unsupported";
    default:                                         return NULL;
  }
}

void aic_check(enum AicErrorCode code) {
  if (code == AIC_ERROR_CODE_SUCCESS) return;
  const char *name = code_to_sym(code);
  VALUE arg = name ? ID2SYM(rb_intern(name)) : INT2NUM((int)code);
  rb_funcall(mAicoustics, id_check_bang, 1, arg);
}

/* ---- shared helpers ------------------------------------------------- */

size_t ivar_sizet(VALUE self, const char *name, VALUE override) {
  if (!NIL_P(override)) return NUM2SIZET(override);
  VALUE value = rb_ivar_get(self, rb_intern(name));
  if (NIL_P(value)) rb_raise(rb_eRuntimeError, "not configured; call #configure first");
  return NUM2SIZET(value);
}

uint16_t checked_channels(size_t num_channels) {
  if (num_channels == 0 || num_channels > UINT16_MAX) {
    rb_raise(rb_eArgError, "num_channels must be between 1 and %u", (unsigned)UINT16_MAX);
  }
  return (uint16_t)num_channels;
}

size_t checked_sample_count(size_t num_frames, size_t num_channels) {
  if (num_frames != 0 && num_channels > SIZE_MAX / num_frames) {
    rb_raise(rb_eArgError, "num_frames * num_channels overflows size_t");
  }
  size_t count = num_frames * num_channels;
  if (count > SIZE_MAX / sizeof(float)) {
    rb_raise(rb_eArgError, "buffer byte size overflows size_t");
  }
  return count;
}

/* ---- module-level --------------------------------------------------- */

static VALUE m_sdk_version(VALUE self) { (void)self; return rb_utf8_str_new_cstr(aic_get_sdk_version()); }
static VALUE m_compatible_model_version(VALUE self) { (void)self; return UINT2NUM(aic_get_compatible_model_version()); }

void Init_aicoustics_ext(void) {
  id_check_bang = rb_intern("check!");
  id_optimal_num_frames = rb_intern("optimal_num_frames");
  id_from_h = rb_intern("from_h");
  id_enable = rb_intern("enable");
  id_session_id = rb_intern("session_id");
  id_export_interval_ms = rb_intern("export_interval_ms");

  mAicoustics = rb_define_module("Aicoustics");
  rb_define_singleton_method(mAicoustics, "sdk_version", m_sdk_version, 0);
  rb_define_singleton_method(mAicoustics, "compatible_model_version", m_compatible_model_version, 0);

  init_model();
  init_processor();
  init_analyzer();
}
