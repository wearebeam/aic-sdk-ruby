#include <ruby.h>
#include <ruby/util.h>
#include <ruby/thread.h>
#include <string.h>
#include "aic.h"

/*
 * Native C extension for the ai-coustics SDK. Owns every SDK handle as a
 * TypedData object whose free function calls the matching aic_*_destroy, and
 * exposes the full ergonomic API (Model, Processor, ProcessorContext,
 * VadContext, Analyzer) directly.
 *
 * Audio buffers cross the boundary as binary Strings of little-endian float32
 * samples (process_*! mutate in place) or as Arrays of Float (#process).
 *
 * Error handling: every aic_* call routes through aic_check, which maps the
 * AicErrorCode to the symbol used by the Ruby ERROR_TABLE and delegates to
 * Aicoustics.check!, keeping all error policy in one place (lib/.../errors.rb).
 */

/* aic_set_sdk_wrapper_id is present in the binary but absent from aic.h. */
extern void aic_set_sdk_wrapper_id(const char *wrapper_id);

static VALUE mAicoustics;
static VALUE cModel, cProcessor, cProcessorContext, cVadContext, cAnalyzer;

static ID id_check_bang, id_new, id_optimal_num_frames, id_from_h;
static ID id_enable, id_session_id, id_export_interval_ms;

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

static void aic_check(enum AicErrorCode code) {
  if (code == AIC_ERROR_CODE_SUCCESS) return;
  const char *name = code_to_sym(code);
  VALUE arg = name ? ID2SYM(rb_intern(name)) : INT2NUM((int)code);
  rb_funcall(mAicoustics, id_check_bang, 1, arg);
}

/* ---- Model ---------------------------------------------------------- */

typedef struct { struct AicModel *handle; } model_t;

static void model_free(void *p) {
  model_t *m = (model_t *)p;
  aic_model_destroy(m->handle);
  xfree(m);
}
static size_t model_memsize(const void *p) { (void)p; return sizeof(model_t); }
static const rb_data_type_t model_type = {
  "Aicoustics::Model",
  { NULL, model_free, model_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static struct AicModel *model_ptr(VALUE self) {
  model_t *m;
  TypedData_Get_Struct(self, model_t, &model_type, m);
  if (!m->handle) rb_raise(rb_eRuntimeError, "Aicoustics::Model handle is null");
  return m->handle;
}

static VALUE model_wrap(struct AicModel *handle) {
  model_t *m = ALLOC(model_t);
  m->handle = handle;
  return TypedData_Wrap_Struct(cModel, &model_type, m);
}

static VALUE model_from_file(VALUE klass, VALUE path) {
  (void)klass;
  struct AicModel *h = NULL;
  aic_check(aic_model_create_from_file(&h, StringValueCStr(path)));
  return model_wrap(h);
}

/* The SDK requires the model buffer to be 64-byte aligned and to stay valid
 * for the model's lifetime. We copy the bytes into an aligned, retained String. */
static VALUE model_from_buffer(VALUE klass, VALUE bytes) {
  (void)klass;
  StringValue(bytes);
  long len = RSTRING_LEN(bytes);

  /* over-allocate and align the copy to 64 bytes */
  const size_t alignment = 64;
  VALUE backing = rb_str_buf_new(len + alignment);
  char *base = RSTRING_PTR(backing);
  size_t pad = (alignment - ((uintptr_t)base % alignment)) % alignment;
  uint8_t *aligned = (uint8_t *)base + pad;
  memcpy(aligned, RSTRING_PTR(bytes), len);

  struct AicModel *h = NULL;
  aic_check(aic_model_create_from_buffer(&h, aligned, (size_t)len));

  VALUE model = model_wrap(h);
  /* keep the aligned copy alive for the model's lifetime */
  rb_ivar_set(model, rb_intern("@backing_buffer"), backing);
  return model;
}

static VALUE model_id(VALUE self) {
  const char *id = aic_model_get_id(model_ptr(self));
  return id ? rb_utf8_str_new_cstr(id) : Qnil;
}

static VALUE model_optimal_sample_rate(VALUE self) {
  uint32_t rate = 0;
  aic_check(aic_model_get_optimal_sample_rate(model_ptr(self), &rate));
  return UINT2NUM(rate);
}

static VALUE model_optimal_num_frames(int argc, VALUE *argv, VALUE self) {
  VALUE rate_arg;
  rb_scan_args(argc, argv, "01", &rate_arg);
  uint32_t rate;
  if (NIL_P(rate_arg)) {
    aic_check(aic_model_get_optimal_sample_rate(model_ptr(self), &rate));
  } else {
    rate = (uint32_t)NUM2UINT(rate_arg);
  }
  size_t frames = 0;
  aic_check(aic_model_get_optimal_num_frames(model_ptr(self), rate, &frames));
  return SIZET2NUM(frames);
}

/* ---- Processor ------------------------------------------------------ */

typedef struct {
  struct AicProcessor *handle;
  VALUE model; /* retained for lifetime + GC marking */
} processor_t;

static void processor_free(void *p) {
  processor_t *s = (processor_t *)p;
  aic_processor_destroy(s->handle);
  xfree(s);
}
static void processor_mark(void *p) { rb_gc_mark(((processor_t *)p)->model); }
static size_t processor_memsize(const void *p) { (void)p; return sizeof(processor_t); }
static const rb_data_type_t processor_type = {
  "Aicoustics::Processor",
  { processor_mark, processor_free, processor_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static processor_t *processor_state(VALUE self) {
  processor_t *s;
  TypedData_Get_Struct(self, processor_t, &processor_type, s);
  return s;
}
static struct AicProcessor *processor_ptr(VALUE self) {
  processor_t *s = processor_state(self);
  if (!s->handle) rb_raise(rb_eRuntimeError, "Aicoustics::Processor handle is null");
  return s->handle;
}

/* Processor.create(model, license_key, otel: nil) */
static VALUE processor_create(int argc, VALUE *argv, VALUE klass) {
  VALUE model, license_key, opts;
  rb_scan_args(argc, argv, "2:", &model, &license_key, &opts);

  VALUE otel = Qnil;
  if (!NIL_P(opts)) otel = rb_hash_aref(opts, ID2SYM(rb_intern("otel")));

  struct AicOtelConfig cfg;
  struct AicOtelConfig *cfg_ptr = NULL;
  VALUE session = Qnil;
  if (!NIL_P(otel)) {
    cfg.enable = RTEST(rb_funcall(otel, id_enable, 0));
    session = rb_funcall(otel, id_session_id, 0);
    cfg.session_id = NIL_P(session) ? NULL : StringValueCStr(session);
    cfg.export_interval_ms = (uint32_t)NUM2UINT(rb_funcall(otel, id_export_interval_ms, 0));
    cfg_ptr = &cfg;
  }

  struct AicProcessor *h = NULL;
  aic_check(aic_processor_create(&h, model_ptr(model), StringValueCStr(license_key), cfg_ptr));
  RB_GC_GUARD(session);

  processor_t *s = ALLOC(processor_t);
  s->handle = h;
  s->model = model;
  VALUE obj = TypedData_Wrap_Struct(klass, &processor_type, s);
  rb_ivar_set(obj, rb_intern("@model"), model);
  return obj;
}

/* #configure(sample_rate:, num_channels: 1, num_frames: nil, allow_variable_frames: false) */
static VALUE processor_configure(int argc, VALUE *argv, VALUE self) {
  VALUE opts;
  rb_scan_args(argc, argv, "0:", &opts);
  if (NIL_P(opts)) opts = rb_hash_new();

  VALUE sr = rb_hash_aref(opts, ID2SYM(rb_intern("sample_rate")));
  if (NIL_P(sr)) rb_raise(rb_eArgError, "missing keyword: :sample_rate");
  VALUE ch = rb_hash_aref(opts, ID2SYM(rb_intern("num_channels")));
  VALUE nf = rb_hash_aref(opts, ID2SYM(rb_intern("num_frames")));
  VALUE avf = rb_hash_aref(opts, ID2SYM(rb_intern("allow_variable_frames")));

  uint32_t sample_rate = (uint32_t)NUM2UINT(sr);
  uint16_t num_channels = NIL_P(ch) ? 1 : (uint16_t)NUM2UINT(ch);
  size_t num_frames;
  if (NIL_P(nf)) {
    VALUE computed = rb_funcall(processor_state(self)->model, id_optimal_num_frames, 1, sr);
    num_frames = NUM2SIZET(computed);
  } else {
    num_frames = NUM2SIZET(nf);
  }

  aic_check(aic_processor_initialize(processor_ptr(self), sample_rate, num_channels,
                                     num_frames, RTEST(avf)));

  rb_ivar_set(self, rb_intern("@sample_rate"), UINT2NUM(sample_rate));
  rb_ivar_set(self, rb_intern("@num_channels"), UINT2NUM(num_channels));
  rb_ivar_set(self, rb_intern("@num_frames"), SIZET2NUM(num_frames));
  return self;
}

/* GVL-released process call */
typedef struct {
  struct AicProcessor *proc;
  float *audio;
  uint16_t num_channels;
  size_t num_frames;
  enum AicErrorCode rc;
} process_args;

static void *do_process_interleaved(void *arg) {
  process_args *a = (process_args *)arg;
  a->rc = aic_processor_process_interleaved(a->proc, a->audio, a->num_channels, a->num_frames);
  return NULL;
}
static void *do_process_sequential(void *arg) {
  process_args *a = (process_args *)arg;
  a->rc = aic_processor_process_sequential(a->proc, a->audio, a->num_channels, a->num_frames);
  return NULL;
}

static size_t ivar_sizet(VALUE self, const char *name, VALUE override) {
  if (!NIL_P(override)) return NUM2SIZET(override);
  VALUE v = rb_ivar_get(self, rb_intern(name));
  if (NIL_P(v)) rb_raise(rb_eRuntimeError, "processor not configured; call #configure first");
  return NUM2SIZET(v);
}

/* shared body for interleaved/sequential: mutate the binary float32 String in place */
static VALUE processor_process_single(int argc, VALUE *argv, VALUE self,
                                      void *(*fn)(void *)) {
  VALUE buffer, opts;
  rb_scan_args(argc, argv, "1:", &buffer, &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  size_t num_frames = ivar_sizet(self, "@num_frames", rb_hash_aref(opts, ID2SYM(rb_intern("num_frames"))));
  size_t num_channels = ivar_sizet(self, "@num_channels", rb_hash_aref(opts, ID2SYM(rb_intern("num_channels"))));

  StringValue(buffer);
  rb_str_modify(buffer);
  size_t expected = num_frames * num_channels * sizeof(float);
  if ((size_t)RSTRING_LEN(buffer) != expected) {
    rb_raise(rb_eArgError, "buffer is %ld bytes, expected %zu (num_frames*num_channels*4)",
             RSTRING_LEN(buffer), expected);
  }

  process_args a = { processor_ptr(self), (float *)RSTRING_PTR(buffer),
                     (uint16_t)num_channels, num_frames, AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(fn, &a, RUBY_UBF_IO, NULL);
  aic_check(a.rc);
  return buffer;
}

static VALUE processor_process_interleaved(int argc, VALUE *argv, VALUE self) {
  return processor_process_single(argc, argv, self, do_process_interleaved);
}
static VALUE processor_process_sequential(int argc, VALUE *argv, VALUE self) {
  return processor_process_single(argc, argv, self, do_process_sequential);
}

/* #process_planar!(channel_buffers, num_frames: @num_frames) -> channel_buffers */
static VALUE processor_process_planar(int argc, VALUE *argv, VALUE self) {
  VALUE buffers, opts;
  rb_scan_args(argc, argv, "1:", &buffers, &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  Check_Type(buffers, T_ARRAY);
  size_t num_frames = ivar_sizet(self, "@num_frames", rb_hash_aref(opts, ID2SYM(rb_intern("num_frames"))));
  long num_channels = RARRAY_LEN(buffers);

  VALUE tmp;
  float **planes = (float **)rb_alloc_tmp_buffer(&tmp, sizeof(float *) * num_channels);
  for (long i = 0; i < num_channels; i++) {
    VALUE buf = rb_ary_entry(buffers, i);
    StringValue(buf);
    rb_str_modify(buf);
    if ((size_t)RSTRING_LEN(buf) != num_frames * sizeof(float)) {
      rb_free_tmp_buffer(&tmp);
      rb_raise(rb_eArgError, "channel %ld buffer is %ld bytes, expected %zu",
               i, RSTRING_LEN(buf), num_frames * sizeof(float));
    }
    planes[i] = (float *)RSTRING_PTR(buf);
  }
  enum AicErrorCode rc = aic_processor_process_planar(processor_ptr(self), planes,
                                                      (uint16_t)num_channels, num_frames);
  rb_free_tmp_buffer(&tmp);
  aic_check(rc);
  return buffers;
}

/* #process(floats) -> Array<Float>; validates length, runs interleaved */
static VALUE processor_process(VALUE self, VALUE floats) {
  Check_Type(floats, T_ARRAY);
  size_t num_frames = ivar_sizet(self, "@num_frames", Qnil);
  size_t num_channels = ivar_sizet(self, "@num_channels", Qnil);
  size_t expected = num_frames * num_channels;
  if ((size_t)RARRAY_LEN(floats) != expected) {
    rb_raise(rb_eArgError, "expected %zu samples, got %ld", expected, RARRAY_LEN(floats));
  }

  VALUE tmp;
  float *audio = (float *)rb_alloc_tmp_buffer(&tmp, sizeof(float) * expected);
  for (size_t i = 0; i < expected; i++) audio[i] = (float)NUM2DBL(rb_ary_entry(floats, i));

  process_args a = { processor_ptr(self), audio, (uint16_t)num_channels, num_frames,
                     AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(do_process_interleaved, &a, RUBY_UBF_IO, NULL);
  if (a.rc != AIC_ERROR_CODE_SUCCESS) { rb_free_tmp_buffer(&tmp); aic_check(a.rc); }

  VALUE out = rb_ary_new_capa(expected);
  for (size_t i = 0; i < expected; i++) rb_ary_push(out, DBL2NUM((double)audio[i]));
  rb_free_tmp_buffer(&tmp);
  return out;
}

/* ---- ProcessorContext ----------------------------------------------- */

typedef struct {
  struct AicProcessorContext *handle;
  VALUE processor; /* retained so the processor outlives the context */
} pctx_t;

static void pctx_free(void *p) {
  pctx_t *s = (pctx_t *)p;
  aic_processor_context_destroy(s->handle);
  xfree(s);
}
static void pctx_mark(void *p) { rb_gc_mark(((pctx_t *)p)->processor); }
static size_t pctx_memsize(const void *p) { (void)p; return sizeof(pctx_t); }
static const rb_data_type_t pctx_type = {
  "Aicoustics::ProcessorContext",
  { pctx_mark, pctx_free, pctx_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static struct AicProcessorContext *pctx_ptr(VALUE self) {
  pctx_t *s;
  TypedData_Get_Struct(self, pctx_t, &pctx_type, s);
  if (!s->handle) rb_raise(rb_eRuntimeError, "Aicoustics::ProcessorContext handle is null");
  return s->handle;
}

static enum AicProcessorParameter pctx_param(VALUE name) {
  ID id = SYM2ID(rb_to_symbol(name));
  if (id == rb_intern("bypass")) return AIC_PROCESSOR_PARAMETER_BYPASS;
  if (id == rb_intern("enhancement_level")) return AIC_PROCESSOR_PARAMETER_ENHANCEMENT_LEVEL;
  rb_raise(rb_eArgError, "unknown processor parameter: %" PRIsVALUE, name);
}

/* Processor#context (memoized) */
static VALUE processor_context(VALUE self) {
  VALUE existing = rb_ivar_get(self, rb_intern("@context"));
  if (!NIL_P(existing)) return existing;

  struct AicProcessorContext *h = NULL;
  aic_check(aic_processor_context_create(&h, processor_ptr(self)));
  pctx_t *s = ALLOC(pctx_t);
  s->handle = h;
  s->processor = self;
  VALUE obj = TypedData_Wrap_Struct(cProcessorContext, &pctx_type, s);
  rb_ivar_set(obj, rb_intern("@processor"), self);
  rb_ivar_set(self, rb_intern("@context"), obj);
  return obj;
}

static VALUE pctx_reset(VALUE self) {
  aic_check(aic_processor_context_reset(pctx_ptr(self)));
  return self;
}
static VALUE pctx_set_parameter(VALUE self, VALUE param, VALUE value) {
  aic_check(aic_processor_context_set_parameter(pctx_ptr(self), pctx_param(param), (float)NUM2DBL(value)));
  return value;
}
static VALUE pctx_get_parameter(VALUE self, VALUE param) {
  float out = 0.0f;
  aic_check(aic_processor_context_get_parameter(pctx_ptr(self), pctx_param(param), &out));
  return DBL2NUM((double)out);
}
static VALUE pctx_output_delay(VALUE self) {
  size_t out = 0;
  aic_check(aic_processor_context_get_output_delay(pctx_ptr(self), &out));
  return SIZET2NUM(out);
}
static VALUE pctx_update_bearer_token(VALUE self, VALUE token) {
  aic_check(aic_processor_context_update_bearer_token(pctx_ptr(self), StringValueCStr(token)));
  return self;
}

/* ---- VadContext ----------------------------------------------------- */

typedef struct {
  struct AicVadContext *handle;
  VALUE processor;
} vad_t;

static void vad_free(void *p) {
  vad_t *s = (vad_t *)p;
  aic_vad_context_destroy(s->handle);
  xfree(s);
}
static void vad_mark(void *p) { rb_gc_mark(((vad_t *)p)->processor); }
static size_t vad_memsize(const void *p) { (void)p; return sizeof(vad_t); }
static const rb_data_type_t vad_type = {
  "Aicoustics::VadContext",
  { vad_mark, vad_free, vad_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static struct AicVadContext *vad_ptr(VALUE self) {
  vad_t *s;
  TypedData_Get_Struct(self, vad_t, &vad_type, s);
  if (!s->handle) rb_raise(rb_eRuntimeError, "Aicoustics::VadContext handle is null");
  return s->handle;
}

static enum AicVadParameter vad_param(VALUE name) {
  ID id = SYM2ID(rb_to_symbol(name));
  if (id == rb_intern("speech_hold_duration")) return AIC_VAD_PARAMETER_SPEECH_HOLD_DURATION;
  if (id == rb_intern("sensitivity")) return AIC_VAD_PARAMETER_SENSITIVITY;
  if (id == rb_intern("minimum_speech_duration")) return AIC_VAD_PARAMETER_MINIMUM_SPEECH_DURATION;
  rb_raise(rb_eArgError, "unknown VAD parameter: %" PRIsVALUE, name);
}

/* Processor#vad (memoized) */
static VALUE processor_vad(VALUE self) {
  VALUE existing = rb_ivar_get(self, rb_intern("@vad"));
  if (!NIL_P(existing)) return existing;

  struct AicVadContext *h = NULL;
  aic_check(aic_vad_context_create(&h, processor_ptr(self)));
  vad_t *s = ALLOC(vad_t);
  s->handle = h;
  s->processor = self;
  VALUE obj = TypedData_Wrap_Struct(cVadContext, &vad_type, s);
  rb_ivar_set(obj, rb_intern("@processor"), self);
  rb_ivar_set(self, rb_intern("@vad"), obj);
  return obj;
}

static VALUE vad_speech_detected(VALUE self) {
  bool detected = false;
  aic_check(aic_vad_context_is_speech_detected(vad_ptr(self), &detected));
  return detected ? Qtrue : Qfalse;
}
static VALUE vad_set_parameter(VALUE self, VALUE param, VALUE value) {
  aic_check(aic_vad_context_set_parameter(vad_ptr(self), vad_param(param), (float)NUM2DBL(value)));
  return value;
}
static VALUE vad_get_parameter(VALUE self, VALUE param) {
  float out = 0.0f;
  aic_check(aic_vad_context_get_parameter(vad_ptr(self), vad_param(param), &out));
  return DBL2NUM((double)out);
}

/* ---- Analyzer (collector + analyzer pair) --------------------------- */

typedef struct {
  struct AicCollector *collector;
  struct AicAnalyzer *analyzer;
  VALUE model;
} analyzer_t;

static void analyzer_free(void *p) {
  analyzer_t *s = (analyzer_t *)p;
  aic_collector_destroy(s->collector);
  aic_analyzer_destroy(s->analyzer);
  xfree(s);
}
static void analyzer_mark(void *p) { rb_gc_mark(((analyzer_t *)p)->model); }
static size_t analyzer_memsize(const void *p) { (void)p; return sizeof(analyzer_t); }
static const rb_data_type_t analyzer_type = {
  "Aicoustics::Analyzer",
  { analyzer_mark, analyzer_free, analyzer_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static analyzer_t *analyzer_state(VALUE self) {
  analyzer_t *s;
  TypedData_Get_Struct(self, analyzer_t, &analyzer_type, s);
  return s;
}

static VALUE analyzer_create(VALUE klass, VALUE model, VALUE license_key) {
  struct AicCollector *collector = NULL;
  struct AicAnalyzer *analyzer = NULL;
  aic_check(aic_analyzer_pair_create(&collector, &analyzer, model_ptr(model),
                                     StringValueCStr(license_key)));
  analyzer_t *s = ALLOC(analyzer_t);
  s->collector = collector;
  s->analyzer = analyzer;
  s->model = model;
  VALUE obj = TypedData_Wrap_Struct(klass, &analyzer_type, s);
  rb_ivar_set(obj, rb_intern("@model"), model);
  return obj;
}

static VALUE analyzer_configure(int argc, VALUE *argv, VALUE self) {
  VALUE opts;
  rb_scan_args(argc, argv, "0:", &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  VALUE sr = rb_hash_aref(opts, ID2SYM(rb_intern("sample_rate")));
  if (NIL_P(sr)) rb_raise(rb_eArgError, "missing keyword: :sample_rate");
  VALUE ch = rb_hash_aref(opts, ID2SYM(rb_intern("num_channels")));
  VALUE nf = rb_hash_aref(opts, ID2SYM(rb_intern("num_frames")));
  VALUE avf = rb_hash_aref(opts, ID2SYM(rb_intern("allow_variable_frames")));

  uint32_t sample_rate = (uint32_t)NUM2UINT(sr);
  uint16_t num_channels = NIL_P(ch) ? 1 : (uint16_t)NUM2UINT(ch);
  size_t num_frames;
  if (NIL_P(nf)) {
    num_frames = NUM2SIZET(rb_funcall(analyzer_state(self)->model, id_optimal_num_frames, 1, sr));
  } else {
    num_frames = NUM2SIZET(nf);
  }

  aic_check(aic_collector_initialize(analyzer_state(self)->collector, sample_rate,
                                     num_channels, num_frames, RTEST(avf)));
  rb_ivar_set(self, rb_intern("@sample_rate"), UINT2NUM(sample_rate));
  rb_ivar_set(self, rb_intern("@num_channels"), UINT2NUM(num_channels));
  rb_ivar_set(self, rb_intern("@num_frames"), SIZET2NUM(num_frames));
  return self;
}

static VALUE analyzer_buffer(int argc, VALUE *argv, VALUE self,
                             enum AicErrorCode (*fn)(struct AicCollector *, const float *, uint16_t, size_t)) {
  VALUE buffer, opts;
  rb_scan_args(argc, argv, "1:", &buffer, &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  size_t num_frames = ivar_sizet(self, "@num_frames", rb_hash_aref(opts, ID2SYM(rb_intern("num_frames"))));
  size_t num_channels = ivar_sizet(self, "@num_channels", rb_hash_aref(opts, ID2SYM(rb_intern("num_channels"))));

  StringValue(buffer);
  size_t expected = num_frames * num_channels * sizeof(float);
  if ((size_t)RSTRING_LEN(buffer) != expected) {
    rb_raise(rb_eArgError, "buffer is %ld bytes, expected %zu", RSTRING_LEN(buffer), expected);
  }
  aic_check(fn(analyzer_state(self)->collector, (const float *)RSTRING_PTR(buffer),
               (uint16_t)num_channels, num_frames));
  return self;
}
static VALUE analyzer_buffer_interleaved(int argc, VALUE *argv, VALUE self) {
  return analyzer_buffer(argc, argv, self, aic_collector_buffer_interleaved);
}
static VALUE analyzer_buffer_sequential(int argc, VALUE *argv, VALUE self) {
  return analyzer_buffer(argc, argv, self, aic_collector_buffer_sequential);
}

static VALUE analyzer_analyze(VALUE self) {
  struct AicAnalysisResult r;
  memset(&r, 0, sizeof(r));
  aic_check(aic_analyzer_analyze_buffered(analyzer_state(self)->analyzer, &r));

  VALUE h = rb_hash_new();
  rb_hash_aset(h, ID2SYM(rb_intern("risk_score")), DBL2NUM((double)r.risk_score));
  rb_hash_aset(h, ID2SYM(rb_intern("speaker_reverb")), DBL2NUM((double)r.speaker_reverb));
  rb_hash_aset(h, ID2SYM(rb_intern("speaker_loudness")), DBL2NUM((double)r.speaker_loudness));
  rb_hash_aset(h, ID2SYM(rb_intern("interfering_speech")), DBL2NUM((double)r.interfering_speech));
  rb_hash_aset(h, ID2SYM(rb_intern("media_speech")), DBL2NUM((double)r.media_speech));
  rb_hash_aset(h, ID2SYM(rb_intern("noise")), DBL2NUM((double)r.noise));
  rb_hash_aset(h, ID2SYM(rb_intern("packet_loss")), DBL2NUM((double)r.packet_loss));

  VALUE cResult = rb_const_get(mAicoustics, rb_intern("AnalysisResult"));
  return rb_funcall(cResult, id_from_h, 1, h);
}

static VALUE analyzer_reset(VALUE self) {
  aic_check(aic_analyzer_reset(analyzer_state(self)->analyzer));
  return self;
}
static VALUE analyzer_update_bearer_token(VALUE self, VALUE token) {
  aic_check(aic_analyzer_update_bearer_token(analyzer_state(self)->analyzer, StringValueCStr(token)));
  return self;
}

/* ---- module-level --------------------------------------------------- */

static VALUE m_sdk_version(VALUE self) { (void)self; return rb_utf8_str_new_cstr(aic_get_sdk_version()); }
static VALUE m_compatible_model_version(VALUE self) { (void)self; return UINT2NUM(aic_get_compatible_model_version()); }
static VALUE m_set_wrapper_id(VALUE self, VALUE id) { (void)self; aic_set_sdk_wrapper_id(StringValueCStr(id)); return Qnil; }

void Init_aicoustics_ext(void) {
  id_check_bang = rb_intern("check!");
  id_new = rb_intern("new");
  id_optimal_num_frames = rb_intern("optimal_num_frames");
  id_from_h = rb_intern("from_h");
  id_enable = rb_intern("enable");
  id_session_id = rb_intern("session_id");
  id_export_interval_ms = rb_intern("export_interval_ms");

  mAicoustics = rb_define_module("Aicoustics");
  rb_define_singleton_method(mAicoustics, "sdk_version", m_sdk_version, 0);
  rb_define_singleton_method(mAicoustics, "compatible_model_version", m_compatible_model_version, 0);
  rb_define_singleton_method(mAicoustics, "_set_wrapper_id", m_set_wrapper_id, 1);

  cModel = rb_define_class_under(mAicoustics, "Model", rb_cObject);
  rb_undef_alloc_func(cModel);
  rb_define_singleton_method(cModel, "from_file", model_from_file, 1);
  rb_define_singleton_method(cModel, "from_buffer", model_from_buffer, 1);
  rb_define_method(cModel, "id", model_id, 0);
  rb_define_method(cModel, "optimal_sample_rate", model_optimal_sample_rate, 0);
  rb_define_method(cModel, "optimal_num_frames", model_optimal_num_frames, -1);

  cProcessor = rb_define_class_under(mAicoustics, "Processor", rb_cObject);
  rb_undef_alloc_func(cProcessor);
  rb_define_singleton_method(cProcessor, "create", processor_create, -1);
  rb_define_method(cProcessor, "configure", processor_configure, -1);
  rb_define_method(cProcessor, "process", processor_process, 1);
  rb_define_method(cProcessor, "process_interleaved!", processor_process_interleaved, -1);
  rb_define_method(cProcessor, "process_sequential!", processor_process_sequential, -1);
  rb_define_method(cProcessor, "process_planar!", processor_process_planar, -1);
  rb_define_method(cProcessor, "context", processor_context, 0);
  rb_define_method(cProcessor, "vad", processor_vad, 0);
  rb_define_attr(cProcessor, "model", 1, 0);
  rb_define_attr(cProcessor, "sample_rate", 1, 0);
  rb_define_attr(cProcessor, "num_channels", 1, 0);
  rb_define_attr(cProcessor, "num_frames", 1, 0);

  cProcessorContext = rb_define_class_under(mAicoustics, "ProcessorContext", rb_cObject);
  rb_undef_alloc_func(cProcessorContext);
  rb_define_method(cProcessorContext, "reset", pctx_reset, 0);
  rb_define_method(cProcessorContext, "set_parameter", pctx_set_parameter, 2);
  rb_define_method(cProcessorContext, "get_parameter", pctx_get_parameter, 1);
  rb_define_method(cProcessorContext, "output_delay", pctx_output_delay, 0);
  rb_define_method(cProcessorContext, "update_bearer_token", pctx_update_bearer_token, 1);

  cVadContext = rb_define_class_under(mAicoustics, "VadContext", rb_cObject);
  rb_undef_alloc_func(cVadContext);
  rb_define_method(cVadContext, "speech_detected?", vad_speech_detected, 0);
  rb_define_method(cVadContext, "set_parameter", vad_set_parameter, 2);
  rb_define_method(cVadContext, "get_parameter", vad_get_parameter, 1);

  cAnalyzer = rb_define_class_under(mAicoustics, "Analyzer", rb_cObject);
  rb_undef_alloc_func(cAnalyzer);
  rb_define_singleton_method(cAnalyzer, "create", analyzer_create, 2);
  rb_define_method(cAnalyzer, "configure", analyzer_configure, -1);
  rb_define_method(cAnalyzer, "buffer_interleaved!", analyzer_buffer_interleaved, -1);
  rb_define_method(cAnalyzer, "buffer_sequential!", analyzer_buffer_sequential, -1);
  rb_define_method(cAnalyzer, "analyze", analyzer_analyze, 0);
  rb_define_method(cAnalyzer, "reset", analyzer_reset, 0);
  rb_define_method(cAnalyzer, "update_bearer_token", analyzer_update_bearer_token, 1);
  rb_define_attr(cAnalyzer, "model", 1, 0);
  rb_define_attr(cAnalyzer, "sample_rate", 1, 0);
  rb_define_attr(cAnalyzer, "num_channels", 1, 0);
  rb_define_attr(cAnalyzer, "num_frames", 1, 0);
}
