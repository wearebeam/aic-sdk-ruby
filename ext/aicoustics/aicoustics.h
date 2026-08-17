#ifndef AICOUSTICS_EXT_H
#define AICOUSTICS_EXT_H

#include <ruby.h>
#include "aic.h"

/*
 * Shared declarations for the ai-coustics C extension. Each SDK handle type lives
 * in its own translation unit (model.c, processor.c, vad.c, analyzer.c); this
 * header exposes the handful of symbols they share. mkmf compiles every *.c in
 * this dir.
 */

/* Module + class handles. Defined in aicoustics_ext.c; assigned in the init_* funcs. */
extern VALUE mAicoustics;
extern VALUE cModel, cProcessor, cProcessorContext, cVad, cVadContext, cAnalyzer;

/* Cached method/keyword identifiers, interned once in Init_aicoustics_ext. */
extern ID id_check_bang, id_optimal_block_size, id_from_h;
extern ID id_enable, id_session_id, id_export_interval_ms;

/* Map an AicErrorCode to a typed Ruby exception via Aicoustics.check! (no-op on success). */
void aic_check(enum AicErrorCode code);

/* Unwrap a Model handle. Defined in model.c; used when creating processors/analyzers. */
struct AicModel *model_ptr(VALUE self);

/* Read a size_t config ivar (e.g. "@block_size"), or use override when non-nil. */
size_t ivar_sizet(VALUE self, const char *name, VALUE override);

/* block_size in float32 bytes, guarded so the multiplication cannot overflow size_t. */
size_t checked_block_bytes(size_t block_size);

/* Fill `config` from an OtelConfig-like object; returns NULL when `otel` is nil.
 * `session_guard` receives the session-id String so the caller can RB_GC_GUARD it
 * for as long as config->session_id is in use. */
struct AicOtelConfig *otel_config_from(VALUE otel, struct AicOtelConfig *config, VALUE *session_guard);

/* Per-type class registration, called from Init_aicoustics_ext. */
void init_model(void);
void init_processor(void); /* Processor + ProcessorContext */
void init_vad(void);       /* Vad + VadContext */
void init_analyzer(void);

#endif
