#ifndef MKS_VM_PREDICT_H
#define MKS_VM_PREDICT_H

#include <stdint.h>

/* Branch prediction hints for compiler */
#if defined(__GNUC__) || defined(__clang__)
#define MKS_LIKELY(x)   __builtin_expect(!!(x), 1)
#define MKS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define MKS_LIKELY(x)   (x)
#define MKS_UNLIKELY(x) (x)
#endif

/* Predicted type for local slots */
typedef enum {
    PRED_UNKNOWN,
    PRED_NUMBER,
    PRED_BOOL,
    PRED_STRING,
    PRED_ARRAY,
    PRED_OBJECT,
    PRED_NULL
} VMPredictedType;

/* Per-slot type prediction metadata */
typedef struct {
    uint8_t predicted_type;
    uint16_t hits;
    uint16_t misses;
} VMSlotPredict;

/* Branch prediction statistics */
typedef struct {
    uint32_t taken;
    uint32_t not_taken;
} VMBranchPredict;

/* Inline cache entry for field access (future use) */
typedef struct {
    uint32_t shape_id;
    uint16_t field_index;
    uint32_t hits;
    uint32_t misses;
} VMFieldInlineCache;

#endif
