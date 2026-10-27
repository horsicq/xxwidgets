/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_optimization_options.h"
#include "xxwidgets/xxwidgets_combobox.h"

#include <limits.h>
#include <stdlib.h>
#include <wchar.h>

typedef enum optimization_layout {
    OPTIMIZATION_LAYOUT_MIXED,
    OPTIMIZATION_LAYOUT_X86,
    OPTIMIZATION_LAYOUT_ARM
} optimization_layout;

struct xxwidgets_optimization_options_widget {
    xxwidgets_widget *owner;
    xxwidgets_widget *controls[XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT];
    xxwidgets_optimization_capabilities capabilities;
    optimization_layout layout;
};

static optimization_layout target_layout(const xxwidgets_optimization_capabilities *capabilities)
{
#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(_M_ARM) || defined(__aarch64__) || defined(__arm__)
    (void)capabilities;
    return OPTIMIZATION_LAYOUT_ARM;
#elif defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    (void)capabilities;
    return OPTIMIZATION_LAYOUT_X86;
#else
    if (capabilities &&
        !(capabilities->sse2 || capabilities->avx2 || capabilities->sse42 || capabilities->aes ||
          capabilities->sha || capabilities->vpclmulqdq) &&
        (capabilities->neon || capabilities->arm_crc32 || capabilities->arm_crypto || capabilities->arm_dotprod || capabilities->arm_pmull))
        return OPTIMIZATION_LAYOUT_ARM;
    return OPTIMIZATION_LAYOUT_MIXED;
#endif
}

static int x86_control(xxwidgets_optimization_options_control_id control)
{
    return control >= XXWIDGETS_OPTIMIZATION_OPTIONS_SSE2 &&
        control <= XXWIDGETS_OPTIMIZATION_OPTIONS_VPCLMULQDQ;
}

static int arm_control(xxwidgets_optimization_options_control_id control)
{
    return control >= XXWIDGETS_OPTIMIZATION_OPTIONS_NEON &&
        control <= XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_PMULL;
}

static int boolean(int value)
{
    return value == 0 || value == 1;
}
static int valid_capabilities(const xxwidgets_optimization_capabilities *capabilities)
{
    return capabilities && boolean(capabilities->sse2) && boolean(capabilities->avx2) &&
        boolean(capabilities->sse42) && boolean(capabilities->aes) && boolean(capabilities->sha) &&
        boolean(capabilities->vpclmulqdq) && boolean(capabilities->neon) &&
        boolean(capabilities->arm_crc32) && boolean(capabilities->arm_crypto) &&
        boolean(capabilities->arm_dotprod) && boolean(capabilities->arm_pmull);
}
static int valid_options(const xxwidgets_optimization_options *options)
{
    return options && boolean(options->use_sse2) && boolean(options->use_avx2) &&
        boolean(options->use_sse42) && boolean(options->use_aes) && boolean(options->use_sha) &&
        boolean(options->use_vpclmulqdq) && boolean(options->use_neon) &&
        boolean(options->use_arm_crc32) && boolean(options->use_arm_crypto) &&
        boolean(options->use_arm_dotprod) && boolean(options->use_arm_pmull);
}
static int layout_height(optimization_layout layout)
{
    return layout == OPTIMIZATION_LAYOUT_ARM ? 9 : layout == OPTIMIZATION_LAYOUT_X86 ? 10 : 12;
}

static int valid_bounds(xxwidgets_rect bounds, optimization_layout layout)
{
    return bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 60 && bounds.height >= layout_height(layout) &&
        bounds.x <= INT_MAX - bounds.width && bounds.y <= INT_MAX - bounds.height;
}

static void option_rects(xxwidgets_rect bounds, optimization_layout layout, xxwidgets_rect *rects)
{
    int first = (bounds.width - 2) / 2, second = bounds.width - first - 2;
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_BUFFER_LABEL] = (xxwidgets_rect){bounds.x, bounds.y, first, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_FILE_BUFFER_LABEL] = (xxwidgets_rect){bounds.x + first + 2, bounds.y, second, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_BUFFER_SIZE] = (xxwidgets_rect){bounds.x, bounds.y + 1, first, 2};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_FILE_BUFFER_SIZE] = (xxwidgets_rect){bounds.x + first + 2, bounds.y + 1, second, 2};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_SIMD_LABEL] = (xxwidgets_rect){bounds.x, bounds.y + 4, bounds.width, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_SSE2] = (xxwidgets_rect){bounds.x, bounds.y + 6, first, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_SSE42] = (xxwidgets_rect){bounds.x + first + 2, bounds.y + 6, second, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_AVX2] = (xxwidgets_rect){bounds.x, bounds.y + 7, first, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_AES] = (xxwidgets_rect){bounds.x + first + 2, bounds.y + 7, second, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_SHA] = (xxwidgets_rect){bounds.x, bounds.y + 8, first, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_VPCLMULQDQ] = (xxwidgets_rect){bounds.x + first + 2, bounds.y + 8, second, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_NEON] = (xxwidgets_rect){bounds.x, bounds.y + (layout != OPTIMIZATION_LAYOUT_MIXED ? 6 : 9), first, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_CRC32] = (xxwidgets_rect){bounds.x + first + 2, bounds.y + (layout != OPTIMIZATION_LAYOUT_MIXED ? 6 : 9), second, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_CRYPTO] = (xxwidgets_rect){bounds.x, bounds.y + (layout != OPTIMIZATION_LAYOUT_MIXED ? 7 : 10), first, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_DOTPROD] = (xxwidgets_rect){bounds.x + first + 2, bounds.y + (layout != OPTIMIZATION_LAYOUT_MIXED ? 7 : 10), second, 1};
    rects[XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_PMULL] = (xxwidgets_rect){bounds.x, bounds.y + (layout != OPTIMIZATION_LAYOUT_MIXED ? 8 : 11), first, 1};
}

static void recover_control(xxwidgets_widget *control)
{
    ++control->app->syncing;
    control->app->ops->sync(control);
    --control->app->syncing;
}

xxwidgets_status xxwidgets_optimization_options_init(xxwidgets_optimization_options *options)
{
    if (!options) return XXWIDGETS_INVALID_ARGUMENT;
    options->buffer_size = 64u * 1024u;
    options->file_buffer_size = 64u * 1024u;
    options->use_sse2 = options->use_avx2 = options->use_sse42 = options->use_aes = options->use_sha =
        options->use_vpclmulqdq = options->use_neon = options->use_arm_crc32 = options->use_arm_crypto =
        options->use_arm_dotprod = 1;
        options->use_arm_pmull = 1;
    return XXWIDGETS_OK;
}

xxwidgets_widget *xxwidgets_optimization_options_widget_control(const xxwidgets_optimization_options_widget *widget, xxwidgets_optimization_options_control_id control)
{
    return widget && control >= 0 && control < XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT ? widget->controls[control] : NULL;
}

static xxwidgets_status get_size(xxwidgets_widget *control, size_t *size)
{
    xx_var value = {0};
    xxwidgets_status status = xxwidgets_combobox_get_current(control, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value.type != XX_VAR_TYPE_UINT64 || value.val.u64 > SIZE_MAX) return XXWIDGETS_INVALID_ARGUMENT;
    *size = (size_t)value.val.u64;
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_optimization_options_widget_get(const xxwidgets_optimization_options_widget *widget, xxwidgets_optimization_options *options)
{
    xxwidgets_optimization_options result = {0};
    xxwidgets_status status;
    if (!widget || !options) return XXWIDGETS_INVALID_ARGUMENT;
    status = get_size(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_BUFFER_SIZE], &result.buffer_size);
    if (status == XXWIDGETS_OK) status = get_size(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_FILE_BUFFER_SIZE], &result.file_buffer_size);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_SSE2], &result.use_sse2);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_AVX2], &result.use_avx2);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_SSE42], &result.use_sse42);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_AES], &result.use_aes);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_SHA], &result.use_sha);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_VPCLMULQDQ], &result.use_vpclmulqdq);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_NEON], &result.use_neon);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_CRC32], &result.use_arm_crc32);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_CRYPTO], &result.use_arm_crypto);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_DOTPROD], &result.use_arm_dotprod);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_get_value(widget->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_ARM_PMULL], &result.use_arm_pmull);
    if (status != XXWIDGETS_OK) return status;
    if (!widget->capabilities.sse2) result.use_sse2 = 0;
    if (!widget->capabilities.avx2) result.use_avx2 = 0;
    if (!widget->capabilities.sse42) result.use_sse42 = 0;
    if (!widget->capabilities.aes) result.use_aes = 0;
    if (!widget->capabilities.sha) result.use_sha = 0;
    if (!widget->capabilities.vpclmulqdq) result.use_vpclmulqdq = 0;
    if (!widget->capabilities.neon) result.use_neon = 0;
    if (!widget->capabilities.arm_crc32) result.use_arm_crc32 = 0;
    if (!widget->capabilities.arm_crypto) result.use_arm_crypto = 0;
    if (!widget->capabilities.arm_pmull) result.use_arm_pmull = 0;
    if (!widget->capabilities.arm_dotprod) result.use_arm_dotprod = 0;
    *options = result;
    return XXWIDGETS_OK;
}

static xxwidgets_status set_size(xxwidgets_widget *control, size_t size)
{
    xx_meta_string records[23] = {0};
    xx_str_w_s labels[23] = {0};
    wchar_t names[23][48];
    size_t i, count = 22;
    int selected = size ? -1 : 0;
    xxwidgets_status status;
    wmemcpy(names[0], L"Default (64 KiB)", 17);
    for (i = 1; i < 22; ++i) {
        size_t bytes = (size_t)1024 << (i - 1);
        records[i].var.val.u64 = (uint64_t)bytes;
        if (bytes >= (size_t)1024 * 1024 * 1024) swprintf(names[i], 48, L"%zu GiB", bytes / ((size_t)1024 * 1024 * 1024));
        else if (bytes >= (size_t)1024 * 1024) swprintf(names[i], 48, L"%zu MiB", bytes / ((size_t)1024 * 1024));
        else swprintf(names[i], 48, L"%zu KiB", bytes / 1024);
        if (size == bytes) selected = (int)i;
    }
    if (selected < 0) {
        swprintf(names[22], 48, L"%zu bytes (custom)", size);
        records[22].var.val.u64 = (uint64_t)size;
        selected = 22;
        count = 23;
    }
    for (i = 0; i < count; ++i) {
        labels[i].data = names[i];
        labels[i].length = wcslen(names[i]);
        labels[i].capacity = labels[i].length + 1;
        labels[i].is_view = true;
        records[i].meta_string = labels + i;
        records[i].var.type = XX_VAR_TYPE_UINT64;
    }
    status = xxwidgets_combobox_set_records(control, records, count);
    return status == XXWIDGETS_OK ? xxwidgets_widget_set_value(control, selected) : status;
}

/* Retain both previous record sets until the entire form update succeeds.
 * Recovery needs no allocation and restores borrowed records on failure. */
typedef struct combo_snapshot {
    struct xxwidgets_combo_state *combo;
    char **items;
    size_t count;
    int value;
} combo_snapshot;

static void retain_combo(xxwidgets_widget *control, combo_snapshot *saved)
{
    saved->combo = control->combo;
    saved->items = control->items;
    saved->count = control->item_count;
    saved->value = control->value;
    control->combo = NULL;
    control->items = NULL;
    control->item_count = 0;
    control->value = -1;
}

static void dispose_saved(xxwidgets_widget *control, combo_snapshot *saved)
{
    xxwidgets_widget old = *control;
    size_t i;
    old.combo = saved->combo;
    xxwidgets_combobox_dispose(&old);
    for (i = 0; i < saved->count; ++i) free(saved->items[i]);
    free(saved->items);
}

static void restore_combo(xxwidgets_widget *control, combo_snapshot *saved)
{
    combo_snapshot replacement;
    retain_combo(control, &replacement);
    control->combo = saved->combo;
    control->items = saved->items;
    control->item_count = saved->count;
    control->value = saved->value;
    ++control->combo_revision;
    recover_control(control);
    dispose_saved(control, &replacement);
}

xxwidgets_status xxwidgets_optimization_options_widget_set(xxwidgets_optimization_options_widget *widget, const xxwidgets_optimization_options *options)
{
    combo_snapshot previous[2];
    int previous_value[11], previous_enabled[11];
    int available[11], chosen[11];
    xxwidgets_status status;
    size_t i;
    if (!widget || !valid_options(options)) return XXWIDGETS_INVALID_ARGUMENT;
    available[0] = widget->capabilities.sse2;
    available[1] = widget->capabilities.avx2;
    available[2] = widget->capabilities.sse42;
    available[3] = widget->capabilities.aes;
    available[4] = widget->capabilities.sha;
    available[5] = widget->capabilities.vpclmulqdq;
    available[6] = widget->capabilities.neon;
    available[7] = widget->capabilities.arm_crc32;
    available[8] = widget->capabilities.arm_crypto;
    available[9] = widget->capabilities.arm_dotprod;
    available[10] = widget->capabilities.arm_pmull;
    chosen[0] = options->use_sse2;
    chosen[1] = options->use_avx2;
    chosen[2] = options->use_sse42;
    chosen[3] = options->use_aes;
    chosen[4] = options->use_sha;
    chosen[5] = options->use_vpclmulqdq;
    chosen[6] = options->use_neon;
    chosen[7] = options->use_arm_crc32;
    chosen[8] = options->use_arm_crypto;
    chosen[9] = options->use_arm_dotprod;
    chosen[10] = options->use_arm_pmull;
    for (i = 0; i < 2; ++i) {
        retain_combo(widget->controls[i], &previous[i]);
    }
    for (i = 0; i < 11; ++i) {
        previous_value[i] = widget->controls[i + 2]->value;
        previous_enabled[i] = widget->controls[i + 2]->enabled;
    }
    status = set_size(widget->controls[0], options->buffer_size);
    if (status == XXWIDGETS_OK) status = set_size(widget->controls[1], options->file_buffer_size);
    for (i = 0; i < 11 && status == XXWIDGETS_OK; ++i) {
        status = xxwidgets_widget_set_value(widget->controls[i + 2], available[i] && chosen[i]);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(widget->controls[i + 2], available[i]);
    }
    if (status == XXWIDGETS_OK) {
        for (i = 0; i < 2; ++i) dispose_saved(widget->controls[i], &previous[i]);
    } else {
        for (i = 0; i < 2; ++i) restore_combo(widget->controls[i], &previous[i]);
        for (i = 0; i < 11; ++i) {
            widget->controls[i + 2]->value = previous_value[i];
            widget->controls[i + 2]->enabled = previous_enabled[i];
            recover_control(widget->controls[i + 2]);
        }
    }
    return status;
}

xxwidgets_status xxwidgets_optimization_options_widget_create(xxwidgets_widget *owner, xxwidgets_rect bounds, const xxwidgets_optimization_capabilities *capabilities,
                                                              xxwidgets_optimization_options_widget **out_widget)
{
    static const char *const labels[XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT] = {
        XXW_N_("Buffer size"), XXW_N_("File buffer size"), XXW_N_("Use SSE2"), XXW_N_("Use AVX2"), XXW_N_("Use SSE4.2"), XXW_N_("Use AES"), XXW_N_("Use SHA"), XXW_N_("Use VPCLMULQDQ"),
        XXW_N_("Use Arm NEON"), XXW_N_("Use Arm CRC32"), XXW_N_("Use Arm AES/SHA"), XXW_N_("Use Arm Dot Product"), XXW_N_("Use Arm PMULL"),
        XXW_N_("Buffer size"), XXW_N_("File buffer size"), XXW_N_("CPU acceleration")};
    xxwidgets_optimization_options_widget *widget;
    xxwidgets_optimization_options defaults;
    xxwidgets_rect rects[XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT];
    xxwidgets_status status;
    size_t i;
    if (!out_widget) return XXWIDGETS_INVALID_ARGUMENT;
    *out_widget = NULL;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !valid_capabilities(capabilities) ||
        !valid_bounds(bounds, target_layout(capabilities))) return XXWIDGETS_INVALID_ARGUMENT;
    if (owner->app->dispatch_depth || owner->app->polling || owner->app->syncing) return XXWIDGETS_BUSY;
    widget = (xxwidgets_optimization_options_widget *)calloc(1, sizeof(*widget));
    if (!widget) return XXWIDGETS_OUT_OF_MEMORY;
    widget->owner = owner;
    widget->capabilities = *capabilities;
    widget->layout = target_layout(capabilities);
    option_rects(bounds, widget->layout, rects);
    for (i = 0; i < XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT; ++i) {
        xxwidgets_kind kind = i < 2 ? XXWIDGETS_COMBOBOX : i < 13 ? XXWIDGETS_CHECKBOX : XXWIDGETS_LABEL;
        status = xxwidgets_widget_create(owner->app, owner, kind, XXW_TR(owner->app, labels[i]), rects[i], &widget->controls[i]);
        if (status != XXWIDGETS_OK) goto failed;
        if ((widget->layout == OPTIMIZATION_LAYOUT_ARM && x86_control((xxwidgets_optimization_options_control_id)i)) ||
            (widget->layout == OPTIMIZATION_LAYOUT_X86 && arm_control((xxwidgets_optimization_options_control_id)i))) {
            status = xxwidgets_widget_set_visible(widget->controls[i], 0);
            if (status != XXWIDGETS_OK) goto failed;
        }
    }
    xxwidgets_optimization_options_init(&defaults);
    status = xxwidgets_optimization_options_widget_set(widget, &defaults);
    if (status != XXWIDGETS_OK) goto failed;
    *out_widget = widget;
    return XXWIDGETS_OK;
failed:
    xxwidgets_optimization_options_widget_destroy(widget);
    return status;
}

xxwidgets_status xxwidgets_optimization_options_widget_destroy(xxwidgets_optimization_options_widget *widget)
{
    size_t i;
    if (!widget) return XXWIDGETS_INVALID_ARGUMENT;
    if (widget->owner->app->dispatch_depth || widget->owner->app->polling || widget->owner->app->syncing) return XXWIDGETS_BUSY;
    for (i = 0; i < XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT; ++i) {
        xxwidgets_status status;
        if (!widget->controls[i]) continue;
        status = xxwidgets_widget_destroy(widget->controls[i]);
        if (status != XXWIDGETS_OK) return status;
        widget->controls[i] = NULL;
    }
    free(widget);
    return XXWIDGETS_OK;
}

xxwidgets_status xxwidgets_optimization_options_widget_set_rect(xxwidgets_optimization_options_widget *widget, xxwidgets_rect bounds)
{
    xxwidgets_rect rects[XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT], previous[XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT];
    size_t i;
    if (!widget || !valid_bounds(bounds, widget->layout)) return XXWIDGETS_INVALID_ARGUMENT;
    option_rects(bounds, widget->layout, rects);
    for (i = 0; i < XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT; ++i) previous[i] = widget->controls[i]->rect;
    for (i = 0; i < XXWIDGETS_OPTIMIZATION_OPTIONS_CONTROL_COUNT; ++i) {
        xxwidgets_status status = xxwidgets_widget_set_rect(widget->controls[i], rects[i]);
        if (status != XXWIDGETS_OK) {
            size_t j;
            for (j = 0; j < i; ++j) widget->controls[j]->rect = previous[j];
            for (j = 0; j < i; ++j) recover_control(widget->controls[j]);
            return status;
        }
    }
    return XXWIDGETS_OK;
}

typedef struct optimization_state {
    xxwidgets_optimization_options_widget *form;
    xxwidgets_widget *window, *defaults, *ok, *cancel;
    int done, accepted, defaults_requested;
} optimization_state;

static void optimization_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    optimization_state *state = (optimization_state *)user;
    (void)app;
    if (event->widget == state->window && event->type == XXWIDGETS_EVENT_CLOSE) state->done = 1;
    else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == state->ok) {
            state->accepted = 1;
            state->done = 1;
        } else if (event->widget == state->cancel) state->done = 1;
        else if (event->widget == state->defaults) state->defaults_requested = 1;
    }
}

xxwidgets_status xxwidgets_optimization_options_dialog(xxwidgets_widget *owner, const char *title, const xxwidgets_optimization_capabilities *capabilities,
                                                       xxwidgets_optimization_options *options, int *accepted)
{
    xxwidgets_optimization_options_widget *form = NULL;
    xxwidgets_widget *previous_focus = NULL, *control, *previous_default;
    xxwidgets_optimization_options selected = {0};
    xxwidgets_event_fn previous_event;
    void *previous_user;
    xxwidgets_app *app;
    optimization_state state = {0};
    xxwidgets_status status, cleanup;
    optimization_layout layout;
    int dialog_height, form_height, button_y;
    int owner_enabled, owner_disabled = 0, modal_attached = 0;
    if (accepted) *accepted = 0;
    if (!owner || owner->kind != XXWIDGETS_WINDOW || !title || !accepted || !valid_options(options) || !valid_capabilities(capabilities))
        return XXWIDGETS_INVALID_ARGUMENT;
    layout = target_layout(capabilities);
    form_height = layout_height(layout);
    dialog_height = form_height + 7;
    button_y = form_height + 2;
    if (owner->rect.x > INT_MAX - 66 || owner->rect.y > INT_MAX - dialog_height - 2) return XXWIDGETS_INVALID_ARGUMENT;
    app = owner->app;
    if (app->dispatch_depth || app->polling || app->syncing || app->modal_window || app->quit) return XXWIDGETS_BUSY;
    previous_event = app->on_event;
    previous_user = app->user_data;
    previous_default = app->modal_default;
    owner_enabled = owner->enabled;
    for (control = app->widgets; control; control = control->next)
        if (control->parent == owner && xxwidgets_focusable(control)) {
            previous_focus = control;
            break;
        }
    app->on_event = optimization_event;
    app->user_data = &state;
    status = xxwidgets_widget_create(app, NULL, XXWIDGETS_WINDOW, title,
        (xxwidgets_rect){owner->rect.x + 2, owner->rect.y + 2, 64, dialog_height}, &state.window);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_window = state.window;
    status = xxwidgets_optimization_options_widget_create(state.window, (xxwidgets_rect){2, 1, 60, form_height}, capabilities, &form);
    if (status != XXWIDGETS_OK) goto finish;
    state.form = form;
    status = xxwidgets_optimization_options_widget_set(form, options);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Defaults"), (xxwidgets_rect){2, button_y, 14, 2}, &state.defaults);
    if (status != XXWIDGETS_OK) goto finish;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "OK"), (xxwidgets_rect){38, button_y, 10, 2}, &state.ok);
    if (status != XXWIDGETS_OK) goto finish;
    app->modal_default = state.ok;
    status = xxwidgets_widget_create(app, state.window, XXWIDGETS_BUTTON, XXW_TR(app, "Cancel"), (xxwidgets_rect){50, button_y, 12, 2}, &state.cancel);
    if (status != XXWIDGETS_OK) goto finish;
    if (app->ops->optimization_options_layout) {
        status = app->ops->optimization_options_layout(state.window, 1);
        if (status != XXWIDGETS_OK) goto finish;
    }
    status = xxwidgets_widget_set_enabled(owner, 0);
    if (status != XXWIDGETS_OK) goto finish;
    owner_disabled = 1;
    if (app->ops->modal_owner) {
        status = app->ops->modal_owner(state.window, owner, 1);
        if (status != XXWIDGETS_OK) goto finish;
        modal_attached = 1;
    }
    status = xxwidgets_widget_focus(form->controls[XXWIDGETS_OPTIMIZATION_OPTIONS_BUFFER_SIZE]);
    while (status == XXWIDGETS_OK && !state.done && !app->quit) {
        status = xxwidgets_app_poll(app, 30);
        if (status == XXWIDGETS_OK && state.defaults_requested) {
            xxwidgets_optimization_options defaults;
            state.defaults_requested = 0;
            status = xxwidgets_optimization_options_init(&defaults);
            if (status == XXWIDGETS_OK) status = xxwidgets_optimization_options_widget_set(state.form, &defaults);
        }
        if (status == XXWIDGETS_OK && !state.done && app->ops->optimization_options_layout) status = app->ops->optimization_options_layout(state.window, 0);
    }
    if (status == XXWIDGETS_OK && state.accepted && !app->quit) status = xxwidgets_optimization_options_widget_get(form, &selected);
    else state.accepted = 0;
finish:
    if (owner_disabled) {
        cleanup = xxwidgets_widget_set_enabled(owner, owner_enabled);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (modal_attached) {
        cleanup = app->ops->modal_owner(state.window, owner, 0);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (form) {
        cleanup = xxwidgets_optimization_options_widget_destroy(form);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (state.window) {
        cleanup = xxwidgets_widget_destroy(state.window);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    app->modal_window = NULL;
    app->modal_default = previous_default;
    app->on_event = previous_event;
    app->user_data = previous_user;
    if (previous_focus) xxwidgets_widget_focus(previous_focus);
    if (status == XXWIDGETS_OK && state.accepted) {
        *options = selected;
        *accepted = 1;
    }
    return status;
}
