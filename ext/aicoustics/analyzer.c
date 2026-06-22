#include "aicoustics.h"
#include <string.h> /* memset */

/* Aicoustics::Analyzer — the Tyto collector + analyzer pair, wrapped together. */

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
