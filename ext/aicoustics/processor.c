#include "aicoustics.h"
#include <ruby/thread.h> /* rb_thread_call_without_gvl */

/*
 * Aicoustics::Processor plus the control-plane handle created from it,
 * ProcessorContext. They live together because the context is created from a
 * processor and retains it (GC mark) so it outlives them.
 */

/* ---- Processor ------------------------------------------------------ */

typedef struct {
  struct AicProcessor *handle;
  VALUE model; /* retained for lifetime + GC marking */
} processor_t;

/* Measured RSS cost of one processor (quail-l-16khz-v5: ~1MiB, first-in-process
 * warmup ~4MiB). Processors share the model's weights, so they are far lighter
 * than analyzers — the estimate keeps GC accounting honest without meaningfully
 * raising GC pressure on real-time paths that hold one per session. */
#define PROCESSOR_NATIVE_MEMSIZE_ESTIMATE ((size_t)4 * 1024 * 1024)

static void processor_free(void *ptr) {
  processor_t *data = (processor_t *)ptr;
  aic_processor_destroy(data->handle);
  xfree(data);
  rb_gc_adjust_memory_usage(-(ssize_t)PROCESSOR_NATIVE_MEMSIZE_ESTIMATE);
}
static void processor_mark(void *ptr) { rb_gc_mark(((processor_t *)ptr)->model); }
static size_t processor_memsize(const void *ptr) {
  (void)ptr;
  return sizeof(processor_t) + PROCESSOR_NATIVE_MEMSIZE_ESTIMATE;
}
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
  VALUE session = Qnil;
  struct AicOtelConfig *otel_config_ptr = otel_config_from(otel, &otel_config, &session);

  struct AicProcessor *handle = NULL;
  aic_check(aic_processor_create(&handle, model_ptr(model), StringValueCStr(license_key), otel_config_ptr));
  RB_GC_GUARD(session);

  processor_t *data = ALLOC(processor_t);
  data->handle = handle;
  data->model = model;
  VALUE obj = TypedData_Wrap_Struct(klass, &processor_type, data);
  rb_ivar_set(obj, rb_intern("@model"), model);
  rb_gc_adjust_memory_usage((ssize_t)PROCESSOR_NATIVE_MEMSIZE_ESTIMATE);
  return obj;
}

/* #configure(sample_rate:, block_size: nil, variable_block_size: false) */
static VALUE processor_configure(int argc, VALUE *argv, VALUE self) {
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
    VALUE computed = rb_funcall(processor_state(self)->model, id_optimal_block_size, 1, sample_rate_opt);
    block_size = NUM2SIZET(computed);
  } else {
    block_size = NUM2SIZET(block_size_opt);
  }

  aic_check(aic_processor_initialize(processor_ptr(self), sample_rate, block_size,
                                     RTEST(variable_opt)));

  rb_ivar_set(self, rb_intern("@sample_rate"), UINT2NUM(sample_rate));
  rb_ivar_set(self, rb_intern("@block_size"), SIZET2NUM(block_size));
  return self;
}

/* GVL-released process call */
typedef struct {
  struct AicProcessor *proc;
  float *audio;
  size_t audio_len;
  enum AicErrorCode rc;
} process_args;

static void *do_process(void *arg) {
  process_args *args = (process_args *)arg;
  args->rc = aic_processor_process(args->proc, args->audio, args->audio_len);
  return NULL;
}

/* #process!(buffer, block_size: @block_size) — mutate the binary float32 mono String in place */
static VALUE processor_process_bang(int argc, VALUE *argv, VALUE self) {
  VALUE buffer, opts;
  rb_scan_args(argc, argv, "1:", &buffer, &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  size_t block_size = ivar_sizet(self, "@block_size", rb_hash_aref(opts, ID2SYM(rb_intern("block_size"))));
  size_t expected = checked_block_bytes(block_size);

  StringValue(buffer);
  rb_str_modify(buffer);
  if ((size_t)RSTRING_LEN(buffer) != expected) {
    rb_raise(rb_eArgError, "buffer is %ld bytes, expected %zu (block_size*4)",
             RSTRING_LEN(buffer), expected);
  }

  process_args args = { processor_ptr(self), (float *)RSTRING_PTR(buffer),
                        block_size, AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(do_process, &args, RUBY_UBF_IO, NULL);
  aic_check(args.rc);
  return buffer;
}

/* #process(floats) -> Array<Float>; validates length, converts through a temp buffer */
static VALUE processor_process(VALUE self, VALUE floats) {
  Check_Type(floats, T_ARRAY);
  size_t block_size = ivar_sizet(self, "@block_size", Qnil);
  checked_block_bytes(block_size);
  if ((size_t)RARRAY_LEN(floats) != block_size) {
    rb_raise(rb_eArgError, "expected %zu samples, got %ld", block_size, RARRAY_LEN(floats));
  }

  VALUE tmp_buffer;
  float *audio = (float *)rb_alloc_tmp_buffer(&tmp_buffer, sizeof(float) * block_size);
  for (size_t i = 0; i < block_size; i++) audio[i] = (float)NUM2DBL(rb_ary_entry(floats, i));

  process_args args = { processor_ptr(self), audio, block_size, AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(do_process, &args, RUBY_UBF_IO, NULL);
  if (args.rc != AIC_ERROR_CODE_SUCCESS) { rb_free_tmp_buffer(&tmp_buffer); aic_check(args.rc); }

  VALUE output = rb_ary_new_capa(block_size);
  for (size_t i = 0; i < block_size; i++) rb_ary_push(output, DBL2NUM((double)audio[i]));
  rb_free_tmp_buffer(&tmp_buffer);
  return output;
}

static VALUE processor_terminate_session(VALUE self) {
  aic_check(aic_processor_terminate_session(processor_ptr(self)));
  return self;
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
static VALUE pctx_audio_delay(VALUE self) {
  size_t delay = 0;
  aic_check(aic_processor_context_get_audio_delay(pctx_ptr(self), &delay));
  return SIZET2NUM(delay);
}
static VALUE pctx_update_bearer_token(VALUE self, VALUE token) {
  aic_check(aic_processor_context_update_bearer_token(pctx_ptr(self), StringValueCStr(token)));
  return self;
}

void init_processor(void) {
  cProcessor = rb_define_class_under(mAicoustics, "Processor", rb_cObject);
  rb_undef_alloc_func(cProcessor);
  rb_define_singleton_method(cProcessor, "create", processor_create, -1);
  rb_define_method(cProcessor, "configure", processor_configure, -1);
  rb_define_method(cProcessor, "process", processor_process, 1);
  rb_define_method(cProcessor, "process!", processor_process_bang, -1);
  rb_define_method(cProcessor, "terminate_session", processor_terminate_session, 0);
  rb_define_method(cProcessor, "context", processor_context, 0);
  rb_define_attr(cProcessor, "model", 1, 0);
  rb_define_attr(cProcessor, "sample_rate", 1, 0);
  rb_define_attr(cProcessor, "block_size", 1, 0);

  cProcessorContext = rb_define_class_under(mAicoustics, "ProcessorContext", rb_cObject);
  rb_undef_alloc_func(cProcessorContext);
  rb_define_method(cProcessorContext, "reset", pctx_reset, 0);
  rb_define_method(cProcessorContext, "set_parameter", pctx_set_parameter, 2);
  rb_define_method(cProcessorContext, "get_parameter", pctx_get_parameter, 1);
  rb_define_method(cProcessorContext, "audio_delay", pctx_audio_delay, 0);
  rb_define_method(cProcessorContext, "update_bearer_token", pctx_update_bearer_token, 1);
}
