#include "aicoustics.h"
#include <string.h> /* memset */

/* Aicoustics::Analyzer — the Tyto collector + analyzer pair, wrapped together. */

typedef struct {
  struct AicCollector *collector;
  struct AicAnalyzer *analyzer;
  VALUE model;
} analyzer_t;

static void analyzer_free(void *ptr) {
  analyzer_t *data = (analyzer_t *)ptr;
  aic_collector_destroy(data->collector);
  aic_analyzer_destroy(data->analyzer);
  xfree(data);
}
static void analyzer_mark(void *ptr) { rb_gc_mark(((analyzer_t *)ptr)->model); }
static size_t analyzer_memsize(const void *ptr) { (void)ptr; return sizeof(analyzer_t); }
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
  return obj;
}

static VALUE analyzer_configure(int argc, VALUE *argv, VALUE self) {
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
    num_frames = NUM2SIZET(rb_funcall(analyzer_state(self)->model, id_optimal_num_frames, 1, sample_rate_opt));
  } else {
    num_frames = NUM2SIZET(num_frames_opt);
  }

  aic_check(aic_collector_initialize(analyzer_state(self)->collector, sample_rate,
                                     num_channels, num_frames, RTEST(allow_variable_opt)));
  rb_ivar_set(self, rb_intern("@sample_rate"), UINT2NUM(sample_rate));
  rb_ivar_set(self, rb_intern("@num_channels"), UINT2NUM(num_channels));
  rb_ivar_set(self, rb_intern("@num_frames"), SIZET2NUM(num_frames));
  return self;
}

static VALUE analyzer_buffer(int argc, VALUE *argv, VALUE self,
                             enum AicErrorCode (*buffer_fn)(struct AicCollector *, const float *, uint16_t, size_t)) {
  VALUE buffer, opts;
  rb_scan_args(argc, argv, "1:", &buffer, &opts);
  if (NIL_P(opts)) opts = rb_hash_new();
  size_t num_frames = ivar_sizet(self, "@num_frames", rb_hash_aref(opts, ID2SYM(rb_intern("num_frames"))));
  size_t num_channels = ivar_sizet(self, "@num_channels", rb_hash_aref(opts, ID2SYM(rb_intern("num_channels"))));
  uint16_t channels = checked_channels(num_channels);
  size_t expected = checked_sample_count(num_frames, num_channels) * sizeof(float);

  StringValue(buffer);
  if ((size_t)RSTRING_LEN(buffer) != expected) {
    rb_raise(rb_eArgError, "buffer is %ld bytes, expected %zu", RSTRING_LEN(buffer), expected);
  }
  aic_check(buffer_fn(analyzer_state(self)->collector, (const float *)RSTRING_PTR(buffer),
                      channels, num_frames));
  return self;
}
static VALUE analyzer_buffer_interleaved(int argc, VALUE *argv, VALUE self) {
  return analyzer_buffer(argc, argv, self, aic_collector_buffer_interleaved);
}
static VALUE analyzer_buffer_sequential(int argc, VALUE *argv, VALUE self) {
  return analyzer_buffer(argc, argv, self, aic_collector_buffer_sequential);
}

static VALUE analyzer_analyze(VALUE self) {
  struct AicAnalysisResult result;
  memset(&result, 0, sizeof(result));
  aic_check(aic_analyzer_analyze_buffered(analyzer_state(self)->analyzer, &result));

  VALUE scores = rb_hash_new();
  rb_hash_aset(scores, ID2SYM(rb_intern("risk_score")), DBL2NUM((double)result.risk_score));
  rb_hash_aset(scores, ID2SYM(rb_intern("speaker_reverb")), DBL2NUM((double)result.speaker_reverb));
  rb_hash_aset(scores, ID2SYM(rb_intern("speaker_loudness")), DBL2NUM((double)result.speaker_loudness));
  rb_hash_aset(scores, ID2SYM(rb_intern("interfering_speech")), DBL2NUM((double)result.interfering_speech));
  rb_hash_aset(scores, ID2SYM(rb_intern("media_speech")), DBL2NUM((double)result.media_speech));
  rb_hash_aset(scores, ID2SYM(rb_intern("noise")), DBL2NUM((double)result.noise));
  rb_hash_aset(scores, ID2SYM(rb_intern("packet_loss")), DBL2NUM((double)result.packet_loss));

  VALUE cResult = rb_const_get(mAicoustics, rb_intern("AnalysisResult"));
  return rb_funcall(cResult, id_from_h, 1, scores);
}

static VALUE analyzer_reset(VALUE self) {
  aic_check(aic_analyzer_reset(analyzer_state(self)->analyzer));
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
