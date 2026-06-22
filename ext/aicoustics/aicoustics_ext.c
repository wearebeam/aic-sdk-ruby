#include <ruby.h>
#include <ruby/util.h>
#include "aic.h"

/*
 * Vertical slice of the native C extension: the Model handle plus the two
 * version free-functions. Proves the build, link, and rpath end-to-end before
 * the rest of the SDK surface (Processor, contexts, Analyzer) is ported.
 *
 * Lives under Aicoustics::CExt so it can coexist with the FFI path during the
 * migration without colliding with Aicoustics::Native or Aicoustics::Model.
 */

static VALUE mAicoustics;   /* Aicoustics            (for error classes + check!) */
static VALUE mCExt;         /* Aicoustics::CExt      */
static VALUE cModel;        /* Aicoustics::CExt::Model */

/* Map an AicErrorCode to the symbol the existing Ruby ERROR_TABLE is keyed on,
 * then delegate to Aicoustics.check! so all error mapping stays in one place. */
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

static void aic_check(enum AicErrorCode code) {
  if (code == AIC_ERROR_CODE_SUCCESS) return;
  const char *name = code_to_sym(code);
  if (name) {
    rb_funcall(mAicoustics, rb_intern("check!"), 1, ID2SYM(rb_intern(name)));
  } else {
    rb_funcall(mAicoustics, rb_intern("check!"), 1, INT2NUM((int)code));
  }
}

/* ---- Model: TypedData wrapper over struct AicModel* ---- */

typedef struct {
  struct AicModel *handle;
} aic_model_wrapper;

static void model_free(void *ptr) {
  aic_model_wrapper *w = (aic_model_wrapper *)ptr;
  aic_model_destroy(w->handle); /* NULL-safe per the SDK contract */
  xfree(w);
}

static size_t model_memsize(const void *ptr) {
  (void)ptr;
  return sizeof(aic_model_wrapper);
}

static const rb_data_type_t model_type = {
  "Aicoustics::CExt::Model",
  { NULL, model_free, model_memsize, },
  NULL, NULL,
  RUBY_TYPED_FREE_IMMEDIATELY,
};

static struct AicModel *model_handle(VALUE self) {
  aic_model_wrapper *w;
  TypedData_Get_Struct(self, aic_model_wrapper, &model_type, w);
  if (!w->handle) rb_raise(rb_eRuntimeError, "Aicoustics::CExt::Model handle is null");
  return w->handle;
}

static VALUE model_from_file(VALUE klass, VALUE path) {
  const char *cpath = StringValueCStr(path);
  struct AicModel *handle = NULL;
  aic_check(aic_model_create_from_file(&handle, cpath));

  aic_model_wrapper *w = ALLOC(aic_model_wrapper);
  w->handle = handle;
  return TypedData_Wrap_Struct(klass, &model_type, w);
}

static VALUE model_id(VALUE self) {
  const char *id = aic_model_get_id(model_handle(self));
  return id ? rb_utf8_str_new_cstr(id) : Qnil;
}

static VALUE model_optimal_sample_rate(VALUE self) {
  uint32_t rate = 0;
  aic_check(aic_model_get_optimal_sample_rate(model_handle(self), &rate));
  return UINT2NUM(rate);
}

static VALUE model_optimal_num_frames(int argc, VALUE *argv, VALUE self) {
  VALUE rate_arg;
  rb_scan_args(argc, argv, "01", &rate_arg);

  uint32_t rate;
  if (NIL_P(rate_arg)) {
    aic_check(aic_model_get_optimal_sample_rate(model_handle(self), &rate));
  } else {
    rate = (uint32_t)NUM2UINT(rate_arg);
  }

  size_t frames = 0;
  aic_check(aic_model_get_optimal_num_frames(model_handle(self), rate, &frames));
  return SIZET2NUM(frames);
}

/* ---- module-level free functions ---- */

static VALUE cext_sdk_version(VALUE self) {
  (void)self;
  return rb_utf8_str_new_cstr(aic_get_sdk_version());
}

static VALUE cext_compatible_model_version(VALUE self) {
  (void)self;
  return UINT2NUM(aic_get_compatible_model_version());
}

void Init_aicoustics_ext(void) {
  mAicoustics = rb_define_module("Aicoustics");
  mCExt = rb_define_module_under(mAicoustics, "CExt");

  rb_define_singleton_method(mCExt, "sdk_version", cext_sdk_version, 0);
  rb_define_singleton_method(mCExt, "compatible_model_version", cext_compatible_model_version, 0);

  cModel = rb_define_class_under(mCExt, "Model", rb_cObject);
  rb_undef_alloc_func(cModel); /* only constructible via Model.from_file */
  rb_define_singleton_method(cModel, "from_file", model_from_file, 1);
  rb_define_method(cModel, "id", model_id, 0);
  rb_define_method(cModel, "optimal_sample_rate", model_optimal_sample_rate, 0);
  rb_define_method(cModel, "optimal_num_frames", model_optimal_num_frames, -1);
}
