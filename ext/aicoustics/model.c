#include "aicoustics.h"
#include <string.h> /* memcpy */

/* Aicoustics::Model — TypedData wrapper over struct AicModel*. */

typedef struct { struct AicModel *handle; } model_t;

static void model_free(void *ptr) {
  model_t *data = (model_t *)ptr;
  aic_model_destroy(data->handle);
  xfree(data);
}
static size_t model_memsize(const void *ptr) { (void)ptr; return sizeof(model_t); }
static const rb_data_type_t model_type = {
  "Aicoustics::Model",
  { NULL, model_free, model_memsize },
  NULL, NULL, RUBY_TYPED_FREE_IMMEDIATELY,
};

struct AicModel *model_ptr(VALUE self) {
  model_t *data;
  TypedData_Get_Struct(self, model_t, &model_type, data);
  if (!data->handle) rb_raise(rb_eRuntimeError, "Aicoustics::Model handle is null");
  return data->handle;
}

static VALUE model_wrap(struct AicModel *handle) {
  model_t *data = ALLOC(model_t);
  data->handle = handle;
  return TypedData_Wrap_Struct(cModel, &model_type, data);
}

static VALUE model_from_file(VALUE klass, VALUE path) {
  (void)klass;
  struct AicModel *handle = NULL;
  aic_check(aic_model_create_from_file(&handle, StringValueCStr(path)));
  return model_wrap(handle);
}

/* The SDK requires the model buffer to be 64-byte aligned and to stay valid
 * for the model's lifetime. We copy the bytes into an aligned, retained String. */
static VALUE model_from_buffer(VALUE klass, VALUE bytes) {
  (void)klass;
  StringValue(bytes);
  long length = RSTRING_LEN(bytes);

  /* over-allocate and align the copy to 64 bytes */
  const size_t alignment = 64;
  VALUE backing = rb_str_buf_new(length + alignment);
  char *base = RSTRING_PTR(backing);
  size_t padding = (alignment - ((uintptr_t)base % alignment)) % alignment;
  uint8_t *aligned = (uint8_t *)base + padding;
  memcpy(aligned, RSTRING_PTR(bytes), length);

  struct AicModel *handle = NULL;
  aic_check(aic_model_create_from_buffer(&handle, aligned, (size_t)length));

  VALUE model = model_wrap(handle);
  /* keep the aligned copy alive for the model's lifetime */
  rb_ivar_set(model, rb_intern("@backing_buffer"), backing);
  return model;
}

static VALUE model_id(VALUE self) {
  const char *id_str = aic_model_get_id(model_ptr(self));
  return id_str ? rb_utf8_str_new_cstr(id_str) : Qnil;
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

void init_model(void) {
  cModel = rb_define_class_under(mAicoustics, "Model", rb_cObject);
  rb_undef_alloc_func(cModel);
  rb_define_singleton_method(cModel, "from_file", model_from_file, 1);
  rb_define_singleton_method(cModel, "from_buffer", model_from_buffer, 1);
  rb_define_method(cModel, "id", model_id, 0);
  rb_define_method(cModel, "optimal_sample_rate", model_optimal_sample_rate, 0);
  rb_define_method(cModel, "optimal_num_frames", model_optimal_num_frames, -1);
}
