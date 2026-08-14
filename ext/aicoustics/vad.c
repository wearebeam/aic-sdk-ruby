#include "aicoustics.h"
#include <ruby/thread.h> /* rb_thread_call_without_gvl */

/*
 * Aicoustics::Vad plus the control-plane handle created from it, VadContext.
 * Since SDK 0.21 the VAD is a standalone handle built from a dedicated VAD
 * model, no longer derived from a Processor. The context is created from a
 * VAD and retains it (GC mark) so it outlives them.
 */

/* ---- Vad ------------------------------------------------------------ */

typedef struct {
  struct AicVad *handle;
  VALUE model; /* retained for lifetime + GC marking */
} vad_t;

static void vad_free(void *ptr) {
  vad_t *data = (vad_t *)ptr;
  aic_vad_destroy(data->handle);
  xfree(data);
}
static void vad_mark(void *ptr) { rb_gc_mark(((vad_t *)ptr)->model); }
static size_t vad_memsize(const void *ptr) { (void)ptr; return sizeof(vad_t); }
static const rb_data_type_t vad_type = {
  "Aicoustics::Vad",
  { vad_mark, vad_free, vad_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static vad_t *vad_state(VALUE self) {
  vad_t *data;
  TypedData_Get_Struct(self, vad_t, &vad_type, data);
  return data;
}
static struct AicVad *vad_ptr(VALUE self) {
  vad_t *data = vad_state(self);
  if (!data->handle) rb_raise(rb_eRuntimeError, "Aicoustics::Vad handle is null");
  return data->handle;
}

/* Vad.create(model, license_key, otel: nil) */
static VALUE vad_create(int argc, VALUE *argv, VALUE klass) {
  VALUE model, license_key, opts;
  rb_scan_args(argc, argv, "2:", &model, &license_key, &opts);

  VALUE otel = Qnil;
  if (!NIL_P(opts)) otel = rb_hash_aref(opts, ID2SYM(rb_intern("otel")));

  struct AicOtelConfig otel_config;
  VALUE session = Qnil;
  struct AicOtelConfig *otel_config_ptr = otel_config_from(otel, &otel_config, &session);

  struct AicVad *handle = NULL;
  aic_check(aic_vad_create(&handle, model_ptr(model), StringValueCStr(license_key), otel_config_ptr));
  RB_GC_GUARD(session);

  vad_t *data = ALLOC(vad_t);
  data->handle = handle;
  data->model = model;
  VALUE obj = TypedData_Wrap_Struct(klass, &vad_type, data);
  rb_ivar_set(obj, rb_intern("@model"), model);
  return obj;
}

/* #configure(sample_rate:, block_size: nil, variable_block_size: false) */
static VALUE vad_configure(int argc, VALUE *argv, VALUE self) {
  VALUE opts;
  rb_scan_args(argc, argv, "0:", &opts);
  if (NIL_P(opts)) opts = rb_hash_new();

  VALUE sample_rate_opt = rb_hash_aref(opts, ID2SYM(rb_intern("sample_rate")));
  if (NIL_P(sample_rate_opt)) rb_raise(rb_eArgError, "missing keyword: :sample_rate");
  VALUE block_size_opt = rb_hash_aref(opts, ID2SYM(rb_intern("block_size")));
  VALUE variable_opt = rb_hash_aref(opts, ID2SYM(rb_intern("variable_block_size")));

  uint32_t sample_rate = (uint32_t)NUM2UINT(sample_rate_opt);
  size_t block_size;
  if (NIL_P(block_size_opt)) {
    VALUE computed = rb_funcall(vad_state(self)->model, id_optimal_block_size, 1, sample_rate_opt);
    block_size = NUM2SIZET(computed);
  } else {
    block_size = NUM2SIZET(block_size_opt);
  }

  aic_check(aic_vad_initialize(vad_ptr(self), sample_rate, block_size, RTEST(variable_opt)));

  rb_ivar_set(self, rb_intern("@sample_rate"), UINT2NUM(sample_rate));
  rb_ivar_set(self, rb_intern("@block_size"), SIZET2NUM(block_size));
  return self;
}

/* GVL-released process call (input is read-only; the VAD only updates its prediction) */
typedef struct {
  struct AicVad *vad;
  const float *audio;
  size_t audio_len;
  enum AicErrorCode rc;
} vad_process_args;

static void *do_vad_process(void *arg) {
  vad_process_args *args = (vad_process_args *)arg;
  args->rc = aic_vad_process(args->vad, args->audio, args->audio_len);
  return NULL;
}

/* #process!(buffer, block_size: @block_size) — feed a binary float32 mono String */
static VALUE vad_process_bang(int argc, VALUE *argv, VALUE self) {
  VALUE buffer, opts;
  rb_scan_args(argc, argv, "1:", &buffer, &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  size_t block_size = ivar_sizet(self, "@block_size", rb_hash_aref(opts, ID2SYM(rb_intern("block_size"))));
  size_t expected = checked_block_bytes(block_size);

  StringValue(buffer);
  if ((size_t)RSTRING_LEN(buffer) != expected) {
    rb_raise(rb_eArgError, "buffer is %ld bytes, expected %zu (block_size*4)",
             RSTRING_LEN(buffer), expected);
  }

  vad_process_args args = { vad_ptr(self), (const float *)RSTRING_PTR(buffer),
                            block_size, AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(do_vad_process, &args, RUBY_UBF_IO, NULL);
  aic_check(args.rc);
  return self;
}

static VALUE vad_terminate_session(VALUE self) {
  aic_check(aic_vad_terminate_session(vad_ptr(self)));
  return self;
}

/* ---- VadContext ------------------------------------------------------ */

typedef struct {
  struct AicVadContext *handle;
  VALUE vad; /* retained so the VAD outlives the context */
} vctx_t;

static void vctx_free(void *ptr) {
  vctx_t *data = (vctx_t *)ptr;
  aic_vad_context_destroy(data->handle);
  xfree(data);
}
static void vctx_mark(void *ptr) { rb_gc_mark(((vctx_t *)ptr)->vad); }
static size_t vctx_memsize(const void *ptr) { (void)ptr; return sizeof(vctx_t); }
static const rb_data_type_t vctx_type = {
  "Aicoustics::VadContext",
  { vctx_mark, vctx_free, vctx_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static struct AicVadContext *vctx_ptr(VALUE self) {
  vctx_t *data;
  TypedData_Get_Struct(self, vctx_t, &vctx_type, data);
  if (!data->handle) rb_raise(rb_eRuntimeError, "Aicoustics::VadContext handle is null");
  return data->handle;
}

static enum AicVadParameter vctx_param(VALUE name) {
  ID param_id = SYM2ID(rb_to_symbol(name));
  if (param_id == rb_intern("speech_hold_duration")) return AIC_VAD_PARAMETER_SPEECH_HOLD_DURATION;
  if (param_id == rb_intern("sensitivity")) return AIC_VAD_PARAMETER_SENSITIVITY;
  if (param_id == rb_intern("minimum_speech_duration")) return AIC_VAD_PARAMETER_MINIMUM_SPEECH_DURATION;
  rb_raise(rb_eArgError, "unknown VAD parameter: %" PRIsVALUE, name);
}

/* Vad#context (memoized) */
static VALUE vad_context(VALUE self) {
  VALUE existing = rb_ivar_get(self, rb_intern("@context"));
  if (!NIL_P(existing)) return existing;

  struct AicVadContext *handle = NULL;
  aic_check(aic_vad_context_create(&handle, vad_ptr(self)));
  vctx_t *data = ALLOC(vctx_t);
  data->handle = handle;
  data->vad = self;
  VALUE obj = TypedData_Wrap_Struct(cVadContext, &vctx_type, data);
  rb_ivar_set(obj, rb_intern("@vad"), self);
  rb_ivar_set(self, rb_intern("@context"), obj);
  return obj;
}

static VALUE vctx_speech_detected(VALUE self) {
  bool detected = false;
  aic_check(aic_vad_context_is_speech_detected(vctx_ptr(self), &detected));
  return detected ? Qtrue : Qfalse;
}
static VALUE vctx_raw_vad_probability(VALUE self) {
  float value = 0.0f;
  aic_check(aic_vad_context_get_raw_vad_probability(vctx_ptr(self), &value));
  return DBL2NUM((double)value);
}
static VALUE vctx_prediction_delay(VALUE self) {
  size_t delay = 0;
  aic_check(aic_vad_context_get_prediction_delay(vctx_ptr(self), &delay));
  return SIZET2NUM(delay);
}
static VALUE vctx_reset(VALUE self) {
  aic_check(aic_vad_context_reset(vctx_ptr(self)));
  return self;
}
static VALUE vctx_set_parameter(VALUE self, VALUE param, VALUE value) {
  aic_check(aic_vad_context_set_parameter(vctx_ptr(self), vctx_param(param), (float)NUM2DBL(value)));
  return value;
}
static VALUE vctx_get_parameter(VALUE self, VALUE param) {
  float value = 0.0f;
  aic_check(aic_vad_context_get_parameter(vctx_ptr(self), vctx_param(param), &value));
  return DBL2NUM((double)value);
}
static VALUE vctx_update_bearer_token(VALUE self, VALUE token) {
  aic_check(aic_vad_context_update_bearer_token(vctx_ptr(self), StringValueCStr(token)));
  return self;
}

void init_vad(void) {
  cVad = rb_define_class_under(mAicoustics, "Vad", rb_cObject);
  rb_undef_alloc_func(cVad);
  rb_define_singleton_method(cVad, "create", vad_create, -1);
  rb_define_method(cVad, "configure", vad_configure, -1);
  rb_define_method(cVad, "process!", vad_process_bang, -1);
  rb_define_method(cVad, "terminate_session", vad_terminate_session, 0);
  rb_define_method(cVad, "context", vad_context, 0);
  rb_define_attr(cVad, "model", 1, 0);
  rb_define_attr(cVad, "sample_rate", 1, 0);
  rb_define_attr(cVad, "block_size", 1, 0);

  cVadContext = rb_define_class_under(mAicoustics, "VadContext", rb_cObject);
  rb_undef_alloc_func(cVadContext);
  rb_define_method(cVadContext, "speech_detected?", vctx_speech_detected, 0);
  rb_define_method(cVadContext, "raw_vad_probability", vctx_raw_vad_probability, 0);
  rb_define_method(cVadContext, "prediction_delay", vctx_prediction_delay, 0);
  rb_define_method(cVadContext, "reset", vctx_reset, 0);
  rb_define_method(cVadContext, "set_parameter", vctx_set_parameter, 2);
  rb_define_method(cVadContext, "get_parameter", vctx_get_parameter, 1);
  rb_define_method(cVadContext, "update_bearer_token", vctx_update_bearer_token, 1);
}
