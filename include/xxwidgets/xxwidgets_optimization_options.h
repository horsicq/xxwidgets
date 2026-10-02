#ifndef XXWIDGETS_OPTIMIZATION_OPTIONS_H
#define XXWIDGETS_OPTIMIZATION_OPTIONS_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_optimization_options {
    size_t buffer_size;
    size_t file_buffer_size;
    int use_sse2;
    int use_avx2;
} xxwidgets_optimization_options;

/* The host supplies CPU and OS support, independently of the chosen settings. */
typedef struct xxwidgets_optimization_capabilities {
    int sse2;
    int avx2;
} xxwidgets_optimization_capabilities;

typedef struct xxwidgets_optimization_options_widget xxwidgets_optimization_options_widget;

typedef enum xxwidgets_optimization_options_control_id {
    XXWIDGETS_OPTIMIZATION_OPTIONS_BUFFER_SIZE = 0,
    XXWIDGETS_OPTIMIZATION_OPTIONS_FILE_BUFFER_SIZE,
    XXWIDGETS_OPTIMIZATION_OPTIONS_SSE2,
    XXWIDGETS_OPTIMIZATION_OPTIONS_AVX2,
    XXWIDGETS_OPTIMIZATION_OPTIONS_BUFFER_LABEL,
    XXWIDGETS_OPTIMIZATION_OPTIONS_FILE_BUFFER_LABEL,
    XXWIDGETS_OPTIMIZATION_OPTIONS_SIMD_LABEL,
    XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT
} xxwidgets_optimization_options_control_id;

/* Defaults are 64 KiB for both buffers, with both acceleration choices on. */
XXWIDGETS_API xxwidgets_status xxwidgets_optimization_options_init(xxwidgets_optimization_options *options);

/* Embedded form: two normal combo boxes and acceleration checkboxes. Bounds
 * use text cells and require at least 60 x 9. Capabilities must be 0/1 and are
 * copied. Unsupported features are unchecked and disabled; supported features
 * remain editable even when the user turns them off. Children belong to owner
 * and use its app callback. Destroy the form before its owner/app; borrowed
 * controls must not be destroyed separately. */
XXWIDGETS_API xxwidgets_status xxwidgets_optimization_options_widget_create(xxwidgets_widget *owner,
    xxwidgets_rect bounds, const xxwidgets_optimization_capabilities *capabilities,
    xxwidgets_optimization_options_widget **out_widget);
XXWIDGETS_API xxwidgets_status xxwidgets_optimization_options_widget_destroy(xxwidgets_optimization_options_widget *widget);
XXWIDGETS_API xxwidgets_widget *xxwidgets_optimization_options_widget_control(
    const xxwidgets_optimization_options_widget *widget, xxwidgets_optimization_options_control_id control);
XXWIDGETS_API xxwidgets_status xxwidgets_optimization_options_widget_get(
    const xxwidgets_optimization_options_widget *widget, xxwidgets_optimization_options *options);
/* Buffer records contain UINT64 byte counts representing size_t values.
 * Choices include powers of two from 1 KiB through 1 GiB. Any other nonzero
 * size is preserved by a copied custom record. Zero means Default (64 KiB)
 * and round-trips as zero; resolving that default belongs to the host.
 * Setters copy values atomically and emit no input events. */
XXWIDGETS_API xxwidgets_status xxwidgets_optimization_options_widget_set(
    xxwidgets_optimization_options_widget *widget, const xxwidgets_optimization_options *options);
XXWIDGETS_API xxwidgets_status xxwidgets_optimization_options_widget_set_rect(
    xxwidgets_optimization_options_widget *widget, xxwidgets_rect bounds);

/* Successful OK commits normalized settings. Cancel, Escape, closing, and
 * errors preserve the supplied settings, including unsupported-feature flags.
 * Invoke after returning from event callbacks/polling. */
XXWIDGETS_API xxwidgets_status xxwidgets_optimization_options_dialog(xxwidgets_widget *owner,
    const char *title, const xxwidgets_optimization_capabilities *capabilities,
    xxwidgets_optimization_options *options, int *accepted);

#ifdef __cplusplus
}
#endif
#endif
