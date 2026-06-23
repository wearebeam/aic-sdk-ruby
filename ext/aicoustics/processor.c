#include "aicoustics.h"
#include <ruby/thread.h> /* rb_thread_call_without_gvl */

/*
 * Aicoustics::Processor plus the two control-plane handles created from it,
 * ProcessorContext and VadContext. They live together because the contexts are
 * created from a processor and retain it (GC mark) so it outlives them.
 */

/* ---- Processor ------------------------------------------------------ */

typedef struct {
  struct AicProcessor *handle;
  VALUE model; /* retained for lifetime + GC marking */
} processor_t;

static void processor_free(void *ptr) {
  processor_t *data = (processor_t *)ptr;
  aic_processor_destroy(data->handle);
  xfree(data);
}
static void processor_mark(void *ptr) { rb_gc_mark(((processor_t *)ptr)->model); }
static size_t processor_memsize(const void *ptr) { (void)ptr; return sizeof(processor_t); }
static const rb_data_type_t processor_type = {
  "Aicoustics::Processor",
  { processor_mark, processor_free, processor_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static processor_t *processor_state(VALUE self) {
  processor_t *data;
  TypedData_Get_Struct(self, processor_t, &processor_type, data);
  return data;
}
static struct AicProcessor *processor_ptr(VALUE self) {
  processor_t *data = processor_state(self);
  if (!data->handle) rb_raise(rb_eRuntimeError, "Aicoustics::Processor handle is null");
  return data->handle;
}

/* Processor.create(model, license_key, otel: nil) */
static VALUE processor_create(int argc, VALUE *argv, VALUE klass) {
  VALUE model, license_key, opts;
  rb_scan_args(argc, argv, "2:", &model, &license_key, &opts);

  VALUE otel = Qnil;
  if (!NIL_P(opts)) otel = rb_hash_aref(opts, ID2SYM(rb_intern("otel")));

  struct AicOtelConfig otel_config;
  struct AicOtelConfig *otel_config_ptr = NULL;
  VALUE session = Qnil;
  if (!NIL_P(otel)) {
    otel_config.enable = RTEST(rb_funcall(otel, id_enable, 0));
    session = rb_funcall(otel, id_session_id, 0);
    otel_config.session_id = NIL_P(session) ? NULL : StringValueCStr(session);
    otel_config.export_interval_ms = (uint32_t)NUM2UINT(rb_funcall(otel, id_export_interval_ms, 0));
    otel_config_ptr = &otel_config;
  }

  struct AicProcessor *handle = NULL;
  aic_check(aic_processor_create(&handle, model_ptr(model), StringValueCStr(license_key), otel_config_ptr));
  RB_GC_GUARD(session);

  processor_t *data = ALLOC(processor_t);
  data->handle = handle;
  data->model = model;
  VALUE obj = TypedData_Wrap_Struct(klass, &processor_type, data);
  rb_ivar_set(obj, rb_intern("@model"), model);
  return obj;
}

/* #configure(sample_rate:, num_channels: 1, num_frames: nil, allow_variable_frames: false) */
static VALUE processor_configure(int argc, VALUE *argv, VALUE self) {
  VALUE opts;
  rb_scan_args(argc, argv, "0:", &opts);
  if (NIL_P(opts)) opts = rb_hash_new();

  VALUE sample_rate_opt = rb_hash_aref(opts, ID2SYM(rb_intern("sample_rate")));
  if (NIL_P(sample_rate_opt)) rb_raise(rb_eArgError, "missing keyword: :sample_rate");
  VALUE num_channels_opt = rb_hash_aref(opts, ID2SYM(rb_intern("num_channels")));
  VALUE num_frames_opt = rb_hash_aref(opts, ID2SYM(rb_intern("num_frames")));
  VALUE allow_variable_opt = rb_hash_aref(opts, ID2SYM(rb_intern("allow_variable_frames")));

  uint32_t sample_rate = (uint32_t)NUM2UINT(sample_rate_opt);
  uint16_t num_channels = NIL_P(num_channels_opt) ? 1 : (uint16_t)NUM2UINT(num_channels_opt);
  size_t num_frames;
  if (NIL_P(num_frames_opt)) {
    VALUE computed = rb_funcall(processor_state(self)->model, id_optimal_num_frames, 1, sample_rate_opt);
    num_frames = NUM2SIZET(computed);
  } else {
    num_frames = NUM2SIZET(num_frames_opt);
  }

  aic_check(aic_processor_initialize(processor_ptr(self), sample_rate, num_channels,
                                     num_frames, RTEST(allow_variable_opt)));

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
  process_args *args = (process_args *)arg;
  args->rc = aic_processor_process_interleaved(args->proc, args->audio, args->num_channels, args->num_frames);
  return NULL;
}
static void *do_process_sequential(void *arg) {
  process_args *args = (process_args *)arg;
  args->rc = aic_processor_process_sequential(args->proc, args->audio, args->num_channels, args->num_frames);
  return NULL;
}

/* planar takes a float*-per-channel array, so it needs its own GVL-released bundle */
typedef struct {
  struct AicProcessor *proc;
  float **planes;
  uint16_t num_channels;
  size_t num_frames;
  enum AicErrorCode rc;
} planar_args;

static void *do_process_planar(void *arg) {
  planar_args *args = (planar_args *)arg;
  args->rc = aic_processor_process_planar(args->proc, args->planes, args->num_channels, args->num_frames);
  return NULL;
}

/* shared body for interleaved/sequential: mutate the binary float32 String in place */
static VALUE processor_process_single(int argc, VALUE *argv, VALUE self,
                                      void *(*process_fn)(void *)) {
  VALUE buffer, opts;
  rb_scan_args(argc, argv, "1:", &buffer, &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  size_t num_frames = ivar_sizet(self, "@num_frames", rb_hash_aref(opts, ID2SYM(rb_intern("num_frames"))));
  size_t num_channels = ivar_sizet(self, "@num_channels", rb_hash_aref(opts, ID2SYM(rb_intern("num_channels"))));
  uint16_t channels = checked_channels(num_channels);
  size_t expected = checked_sample_count(num_frames, num_channels) * sizeof(float);

  StringValue(buffer);
  rb_str_modify(buffer);
  if ((size_t)RSTRING_LEN(buffer) != expected) {
    rb_raise(rb_eArgError, "buffer is %ld bytes, expected %zu (num_frames*num_channels*4)",
             RSTRING_LEN(buffer), expected);
  }

  process_args args = { processor_ptr(self), (float *)RSTRING_PTR(buffer),
                        channels, num_frames, AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(process_fn, &args, RUBY_UBF_IO, NULL);
  aic_check(args.rc);
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
  uint16_t channels = checked_channels((size_t)RARRAY_LEN(buffers));
  size_t expected = checked_sample_count(num_frames, 1) * sizeof(float); /* per-channel byte length */

  VALUE tmp_buffer;
  float **planes = (float **)rb_alloc_tmp_buffer(&tmp_buffer, sizeof(float *) * channels);
  for (uint16_t channel = 0; channel < channels; channel++) {
    VALUE channel_buffer = rb_ary_entry(buffers, channel);
    StringValue(channel_buffer);
    rb_str_modify(channel_buffer);
    if ((size_t)RSTRING_LEN(channel_buffer) != expected) {
      rb_free_tmp_buffer(&tmp_buffer);
      rb_raise(rb_eArgError, "channel %u buffer is %ld bytes, expected %zu",
               channel, RSTRING_LEN(channel_buffer), expected);
    }
    planes[channel] = (float *)RSTRING_PTR(channel_buffer);
  }
  planar_args args = { processor_ptr(self), planes, channels, num_frames, AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(do_process_planar, &args, RUBY_UBF_IO, NULL);
  rb_free_tmp_buffer(&tmp_buffer);
  aic_check(args.rc);
  return buffers;
}

/* #process(floats) -> Array<Float>; validates length, runs interleaved */
static VALUE processor_process(VALUE self, VALUE floats) {
  Check_Type(floats, T_ARRAY);
  size_t num_frames = ivar_sizet(self, "@num_frames", Qnil);
  size_t num_channels = ivar_sizet(self, "@num_channels", Qnil);
  uint16_t channels = checked_channels(num_channels);
  size_t sample_count = checked_sample_count(num_frames, num_channels);
  if ((size_t)RARRAY_LEN(floats) != sample_count) {
    rb_raise(rb_eArgError, "expected %zu samples, got %ld", sample_count, RARRAY_LEN(floats));
  }

  VALUE tmp_buffer;
  float *audio = (float *)rb_alloc_tmp_buffer(&tmp_buffer, sizeof(float) * sample_count);
  for (size_t i = 0; i < sample_count; i++) audio[i] = (float)NUM2DBL(rb_ary_entry(floats, i));

  process_args args = { processor_ptr(self), audio, channels, num_frames,
                        AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(do_process_interleaved, &args, RUBY_UBF_IO, NULL);
  if (args.rc != AIC_ERROR_CODE_SUCCESS) { rb_free_tmp_buffer(&tmp_buffer); aic_check(args.rc); }

  VALUE output = rb_ary_new_capa(sample_count);
  for (size_t i = 0; i < sample_count; i++) rb_ary_push(output, DBL2NUM((double)audio[i]));
  rb_free_tmp_buffer(&tmp_buffer);
  return output;
}

/* ---- ProcessorContext ----------------------------------------------- */

typedef struct {
  struct AicProcessorContext *handle;
  VALUE processor; /* retained so the processor outlives the context */
} pctx_t;

static void pctx_free(void *ptr) {
  pctx_t *data = (pctx_t *)ptr;
  aic_processor_context_destroy(data->handle);
  xfree(data);
}
static void pctx_mark(void *ptr) { rb_gc_mark(((pctx_t *)ptr)->processor); }
static size_t pctx_memsize(const void *ptr) { (void)ptr; return sizeof(pctx_t); }
static const rb_data_type_t pctx_type = {
  "Aicoustics::ProcessorContext",
  { pctx_mark, pctx_free, pctx_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static struct AicProcessorContext *pctx_ptr(VALUE self) {
  pctx_t *data;
  TypedData_Get_Struct(self, pctx_t, &pctx_type, data);
  if (!data->handle) rb_raise(rb_eRuntimeError, "Aicoustics::ProcessorContext handle is null");
  return data->handle;
}

static enum AicProcessorParameter pctx_param(VALUE name) {
  ID param_id = SYM2ID(rb_to_symbol(name));
  if (param_id == rb_intern("bypass")) return AIC_PROCESSOR_PARAMETER_BYPASS;
  if (param_id == rb_intern("enhancement_level")) return AIC_PROCESSOR_PARAMETER_ENHANCEMENT_LEVEL;
  rb_raise(rb_eArgError, "unknown processor parameter: %" PRIsVALUE, name);
}

/* Processor#context (memoized) */
static VALUE processor_context(VALUE self) {
  VALUE existing = rb_ivar_get(self, rb_intern("@context"));
  if (!NIL_P(existing)) return existing;

  struct AicProcessorContext *handle = NULL;
  aic_check(aic_processor_context_create(&handle, processor_ptr(self)));
  pctx_t *data = ALLOC(pctx_t);
  data->handle = handle;
  data->processor = self;
  VALUE obj = TypedData_Wrap_Struct(cProcessorContext, &pctx_type, data);
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
  float value = 0.0f;
  aic_check(aic_processor_context_get_parameter(pctx_ptr(self), pctx_param(param), &value));
  return DBL2NUM((double)value);
}
static VALUE pctx_output_delay(VALUE self) {
  size_t delay = 0;
  aic_check(aic_processor_context_get_output_delay(pctx_ptr(self), &delay));
  return SIZET2NUM(delay);
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

static void vad_free(void *ptr) {
  vad_t *data = (vad_t *)ptr;
  aic_vad_context_destroy(data->handle);
  xfree(data);
}
static void vad_mark(void *ptr) { rb_gc_mark(((vad_t *)ptr)->processor); }
static size_t vad_memsize(const void *ptr) { (void)ptr; return sizeof(vad_t); }
static const rb_data_type_t vad_type = {
  "Aicoustics::VadContext",
  { vad_mark, vad_free, vad_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static struct AicVadContext *vad_ptr(VALUE self) {
  vad_t *data;
  TypedData_Get_Struct(self, vad_t, &vad_type, data);
  if (!data->handle) rb_raise(rb_eRuntimeError, "Aicoustics::VadContext handle is null");
  return data->handle;
}

static enum AicVadParameter vad_param(VALUE name) {
  ID param_id = SYM2ID(rb_to_symbol(name));
  if (param_id == rb_intern("speech_hold_duration")) return AIC_VAD_PARAMETER_SPEECH_HOLD_DURATION;
  if (param_id == rb_intern("sensitivity")) return AIC_VAD_PARAMETER_SENSITIVITY;
  if (param_id == rb_intern("minimum_speech_duration")) return AIC_VAD_PARAMETER_MINIMUM_SPEECH_DURATION;
  rb_raise(rb_eArgError, "unknown VAD parameter: %" PRIsVALUE, name);
}

/* Processor#vad (memoized) */
static VALUE processor_vad(VALUE self) {
  VALUE existing = rb_ivar_get(self, rb_intern("@vad"));
  if (!NIL_P(existing)) return existing;

  struct AicVadContext *handle = NULL;
  aic_check(aic_vad_context_create(&handle, processor_ptr(self)));
  vad_t *data = ALLOC(vad_t);
  data->handle = handle;
  data->processor = self;
  VALUE obj = TypedData_Wrap_Struct(cVadContext, &vad_type, data);
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
  float value = 0.0f;
  aic_check(aic_vad_context_get_parameter(vad_ptr(self), vad_param(param), &value));
  return DBL2NUM((double)value);
}

void init_processor(void) {
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
}
