#ifndef AICOUSTICS_EXT_H
#define AICOUSTICS_EXT_H

#include <ruby.h>
#include "aic.h"

/*
 * Shared declarations for the ai-coustics C extension. Each SDK handle type lives
 * in its own translation unit (model.c, processor.c, analyzer.c); this header
 * exposes the handful of symbols they share. mkmf compiles every *.c in this dir.
 */

/* Module + class handles. Defined in aicoustics_ext.c; assigned in the init_* funcs. */
extern VALUE mAicoustics;
extern VALUE cModel, cProcessor, cProcessorContext, cVadContext, cAnalyzer;

/* Cached method/keyword identifiers, interned once in Init_aicoustics_ext. */
extern ID id_check_bang, id_optimal_num_frames, id_from_h;
extern ID id_enable, id_session_id, id_export_interval_ms;

/* Map an AicErrorCode to a typed Ruby exception via Aicoustics.check! (no-op on success). */
void aic_check(enum AicErrorCode code);

/* Unwrap a Model handle. Defined in model.c; used when creating processors/analyzers. */
struct AicModel *model_ptr(VALUE self);

/* Read a size_t config ivar (e.g. "@num_frames"), or use override when non-nil. */
size_t ivar_sizet(VALUE self, const char *name, VALUE override);

/* Per-type class registration, called from Init_aicoustics_ext. */
void init_model(void);
void init_processor(void); /* Processor + ProcessorContext + VadContext */
void init_analyzer(void);

#endif
