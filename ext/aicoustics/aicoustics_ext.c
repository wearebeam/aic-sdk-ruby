#include "aicoustics.h"

/*
 * Entry point and shared infrastructure for the ai-coustics C extension:
 * the module/class handle storage, AicErrorCode -> exception mapping, the
 * config-ivar reader, and Init_aicoustics_ext which wires everything up.
 *
 * The handle types themselves live in model.c, processor.c, vad.c, and
 * analyzer.c. Audio buffers cross the boundary as binary Strings of
 * little-endian float32 samples (process!/buffer! work in place) or as
 * Arrays of Float (#process).
 *
 * Error handling routes through aic_check, which delegates to Aicoustics.check!
 * so all error policy stays in one place (lib/aicoustics/errors.rb).
 */

VALUE mAicoustics;
VALUE cModel, cProcessor, cProcessorContext, cVad, cVadContext, cAnalyzer;

ID id_check_bang, id_optimal_block_size, id_from_h;
ID id_enable, id_session_id, id_export_interval_ms;

/* ---- error mapping -------------------------------------------------- */

static const char *code_to_sym(enum AicErrorCode code) {
  switch (code) {
    case AIC_ERROR_CODE_SUCCESS:                     return "success";
    case AIC_ERROR_CODE_NULL_POINTER:                return "null_pointer";
    case AIC_ERROR_CODE_PARAMETER_OUT_OF_RANGE:      return "parameter_out_of_range";
    case AIC_ERROR_CODE_NOT_INITIALIZED:             return "not_initialized";
    case AIC_ERROR_CODE_AUDIO_CONFIG_UNSUPPORTED:    return "audio_config_unsupported";
    case AIC_ERROR_CODE_AUDIO_CONFIG_MISMATCH:       return "audio_config_mismatch";
    case AIC_ERROR_CODE_PROCESSING_NOT_ALLOWED:      return "processing_not_allowed";
    case AIC_ERROR_CODE_INTERNAL_ERROR:              return "internal_error";
    case AIC_ERROR_CODE_LICENSE_FORMAT_INVALID:      return "license_format_invalid";
    case AIC_ERROR_CODE_LICENSE_VERSION_UNSUPPORTED: return "license_version_unsupported";
    case AIC_ERROR_CODE_LICENSE_EXPIRED:             return "license_expired";
    case AIC_ERROR_CODE_TOKEN_UPDATE_UNSUPPORTED:    return "token_update_unsupported";
    case AIC_ERROR_CODE_MODEL_INVALID:               return "model_invalid";
    case AIC_ERROR_CODE_MODEL_VERSION_UNSUPPORTED:   return "model_version_unsupported";
    case AIC_ERROR_CODE_FILE_PATH_INVALID:           return "file_path_invalid";
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

size_t checked_block_bytes(size_t block_size) {
  if (block_size > SIZE_MAX / sizeof(float)) {
    rb_raise(rb_eArgError, "block byte size overflows size_t");
  }
  return block_size * sizeof(float);
}

struct AicOtelConfig *otel_config_from(VALUE otel, struct AicOtelConfig *config, VALUE *session_guard) {
  if (NIL_P(otel)) return NULL;
  config->enable = RTEST(rb_funcall(otel, id_enable, 0));
  VALUE session = rb_funcall(otel, id_session_id, 0);
  config->session_id = NIL_P(session) ? NULL : StringValueCStr(session);
  config->export_interval_ms = (uint32_t)NUM2UINT(rb_funcall(otel, id_export_interval_ms, 0));
  *session_guard = session;
  return config;
}

/* ---- module-level --------------------------------------------------- */

static VALUE m_sdk_version(VALUE self) { (void)self; return rb_utf8_str_new_cstr(aic_get_sdk_version()); }
static VALUE m_compatible_model_version(VALUE self) { (void)self; return UINT2NUM(aic_get_compatible_model_version()); }

void Init_aicoustics_ext(void) {
  id_check_bang = rb_intern("check!");
  id_optimal_block_size = rb_intern("optimal_block_size");
  id_from_h = rb_intern("from_h");
  id_enable = rb_intern("enable");
  id_session_id = rb_intern("session_id");
  id_export_interval_ms = rb_intern("export_interval_ms");

  mAicoustics = rb_define_module("Aicoustics");
  rb_define_singleton_method(mAicoustics, "sdk_version", m_sdk_version, 0);
  rb_define_singleton_method(mAicoustics, "compatible_model_version", m_compatible_model_version, 0);

  init_model();
  init_processor();
  init_vad();
  init_analyzer();
}
