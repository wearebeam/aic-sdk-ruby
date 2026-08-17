#include "aicoustics.h"
#include <ruby/thread.h> /* rb_thread_call_without_gvl */
#include <string.h> /* memset */

/* Aicoustics::Analyzer — the Tyto collector + analyzer pair, wrapped together. */

typedef struct {
  struct AicCollector *collector;
  struct AicAnalyzer *analyzer;
  VALUE model;
} analyzer_t;

/* Measured RSS cost of one collector+analyzer pair (tyto-1.1-l-16khz: ~8MiB
 * marginal, ~13MiB first-in-process). The SDK exposes no size query, so report
 * a fixed estimate. Without it the GC prices each analyzer at
 * sizeof(analyzer_t) and feels no pressure to collect dead wrappers, each of
 * which pins the full native allocation — a process creating analyzers per
 * unit of work grows unbounded between GC runs. */
#define ANALYZER_NATIVE_MEMSIZE_ESTIMATE ((size_t)13 * 1024 * 1024)

static void analyzer_free(void *ptr) {
  analyzer_t *data = (analyzer_t *)ptr;
  aic_collector_destroy(data->collector);
  aic_analyzer_destroy(data->analyzer);
  xfree(data);
  rb_gc_adjust_memory_usage(-(ssize_t)ANALYZER_NATIVE_MEMSIZE_ESTIMATE);
}
static void analyzer_mark(void *ptr) { rb_gc_mark(((analyzer_t *)ptr)->model); }
static size_t analyzer_memsize(const void *ptr) {
  (void)ptr;
  return sizeof(analyzer_t) + ANALYZER_NATIVE_MEMSIZE_ESTIMATE;
}
static const rb_data_type_t analyzer_type = {
  "Aicoustics::Analyzer",
  { analyzer_mark, analyzer_free, analyzer_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

static analyzer_t *analyzer_state(VALUE self) {
  analyzer_t *data;
  TypedData_Get_Struct(self, analyzer_t, &analyzer_type, data);
  return data;
}

static VALUE analyzer_create(VALUE klass, VALUE model, VALUE license_key) {
  struct AicCollector *collector = NULL;
  struct AicAnalyzer *analyzer = NULL;
  enum AicErrorCode rc = aic_analyzer_pair_create(&collector, &analyzer, model_ptr(model),
                                                  StringValueCStr(license_key));
  if (rc != AIC_ERROR_CODE_SUCCESS) {
    /* destroy whichever handle was created before raising, so we don't leak it */
    if (collector) aic_collector_destroy(collector);
    if (analyzer) aic_analyzer_destroy(analyzer);
    aic_check(rc);
  }

  analyzer_t *data = ALLOC(analyzer_t);
  data->collector = collector;
  data->analyzer = analyzer;
  data->model = model;
  VALUE obj = TypedData_Wrap_Struct(klass, &analyzer_type, data);
  rb_ivar_set(obj, rb_intern("@model"), model);
  rb_gc_adjust_memory_usage((ssize_t)ANALYZER_NATIVE_MEMSIZE_ESTIMATE);
  return obj;
}

/* #configure(sample_rate:, block_size: nil, variable_block_size: false) */
static VALUE analyzer_configure(int argc, VALUE *argv, VALUE self) {
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
    block_size = NUM2SIZET(rb_funcall(analyzer_state(self)->model, id_optimal_block_size, 1, sample_rate_opt));
  } else {
    block_size = NUM2SIZET(block_size_opt);
  }

  aic_check(aic_collector_initialize(analyzer_state(self)->collector, sample_rate,
                                     block_size, RTEST(variable_opt)));
  rb_ivar_set(self, rb_intern("@sample_rate"), UINT2NUM(sample_rate));
  rb_ivar_set(self, rb_intern("@block_size"), SIZET2NUM(block_size));
  return self;
}

/* GVL-released buffer call */
typedef struct {
  struct AicCollector *collector;
  const float *audio;
  size_t audio_len;
  enum AicErrorCode rc;
} buffer_args;

static void *do_buffer(void *arg) {
  buffer_args *args = (buffer_args *)arg;
  args->rc = aic_collector_buffer(args->collector, args->audio, args->audio_len);
  return NULL;
}

/* analyze takes the analyzer handle + a result out-param, so it needs its own GVL-released bundle */
typedef struct {
  struct AicAnalyzer *analyzer;
  struct AicAnalysisResult *result;
  enum AicErrorCode rc;
} analyze_args;

static void *do_analyze(void *arg) {
  analyze_args *args = (analyze_args *)arg;
  args->rc = aic_analyzer_analyze_buffered(args->analyzer, args->result);
  return NULL;
}

/* #buffer!(buffer, block_size: @block_size) — copy the binary float32 mono String into the collector */
static VALUE analyzer_buffer_bang(int argc, VALUE *argv, VALUE self) {
  VALUE buffer, opts;
  rb_scan_args(argc, argv, "1:", &buffer, &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  size_t block_size = ivar_sizet(self, "@block_size", rb_hash_aref(opts, ID2SYM(rb_intern("block_size"))));
  size_t expected = checked_block_bytes(block_size);

  StringValue(buffer);
  if ((size_t)RSTRING_LEN(buffer) != expected) {
    rb_raise(rb_eArgError, "buffer is %ld bytes, expected %zu", RSTRING_LEN(buffer), expected);
  }
  buffer_args args = { analyzer_state(self)->collector, (const float *)RSTRING_PTR(buffer),
                       block_size, AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(do_buffer, &args, RUBY_UBF_IO, NULL);
  aic_check(args.rc);
  return self;
}

static VALUE analyzer_analyze(VALUE self) {
  struct AicAnalysisResult result;
  memset(&result, 0, sizeof(result));
  analyze_args args = { analyzer_state(self)->analyzer, &result, AIC_ERROR_CODE_SUCCESS };
  rb_thread_call_without_gvl(do_analyze, &args, RUBY_UBF_IO, NULL);
  aic_check(args.rc);

  VALUE scores = rb_hash_new();
  rb_hash_aset(scores, ID2SYM(rb_intern("risk_score")), DBL2NUM((double)result.risk_score));
  rb_hash_aset(scores, ID2SYM(rb_intern("speaker_reverb")), DBL2NUM((double)result.speaker_reverb));
  rb_hash_aset(scores, ID2SYM(rb_intern("speaker_loudness")), DBL2NUM((double)result.speaker_loudness));
  rb_hash_aset(scores, ID2SYM(rb_intern("interfering_speech")), DBL2NUM((double)result.interfering_speech));
  rb_hash_aset(scores, ID2SYM(rb_intern("noise")), DBL2NUM((double)result.noise));
  rb_hash_aset(scores, ID2SYM(rb_intern("codec_degradation")), DBL2NUM((double)result.codec_degradation));
  rb_hash_aset(scores, ID2SYM(rb_intern("packet_loss")), DBL2NUM((double)result.packet_loss));

  VALUE cResult = rb_const_get(mAicoustics, rb_intern("AnalysisResult"));
  return rb_funcall(cResult, id_from_h, 1, scores);
}

static VALUE analyzer_reset(VALUE self) {
  aic_check(aic_analyzer_reset(analyzer_state(self)->analyzer));
  return self;
}
static VALUE analyzer_terminate_session(VALUE self) {
  aic_check(aic_analyzer_terminate_session(analyzer_state(self)->analyzer));
  return self;
}
static VALUE analyzer_update_bearer_token(VALUE self, VALUE token) {
  aic_check(aic_analyzer_update_bearer_token(analyzer_state(self)->analyzer, StringValueCStr(token)));
  return self;
}

void init_analyzer(void) {
  cAnalyzer = rb_define_class_under(mAicoustics, "Analyzer", rb_cObject);
  rb_undef_alloc_func(cAnalyzer);
  rb_define_singleton_method(cAnalyzer, "create", analyzer_create, 2);
  rb_define_method(cAnalyzer, "configure", analyzer_configure, -1);
  rb_define_method(cAnalyzer, "buffer!", analyzer_buffer_bang, -1);
  rb_define_method(cAnalyzer, "analyze", analyzer_analyze, 0);
  rb_define_method(cAnalyzer, "reset", analyzer_reset, 0);
  rb_define_method(cAnalyzer, "terminate_session", analyzer_terminate_session, 0);
  rb_define_method(cAnalyzer, "update_bearer_token", analyzer_update_bearer_token, 1);
  rb_define_attr(cAnalyzer, "model", 1, 0);
  rb_define_attr(cAnalyzer, "sample_rate", 1, 0);
  rb_define_attr(cAnalyzer, "block_size", 1, 0);
}
