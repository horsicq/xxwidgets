/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/die_engine/xxwidgets_die_signature_debug_session.h"
#include "xxfclib/js/xx_js_debug.h"
#include "xxfclib/threads/xx_threads.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#define DEBUG_MAX_SOURCE (8u * 1024u * 1024u)
#define DEBUG_MAX_FRAMES 256u
#define DEBUG_MAX_VALUES 4096u
#define DEBUG_MAX_WATCHES 256u
#define DEBUG_MAX_BREAKPOINTS 4096u
#define DEBUG_MAX_VALUE_TEXT 4096u
#define DEBUG_MAX_LOG (1024u * 1024u)

typedef struct debug_breakpoint { char *source_name; size_t line; } debug_breakpoint;
struct xxwidgets_die_signature_debug_session {
    xx_mutex_t *mutex;
    xx_cond_t *cond;
    xx_thread_t *thread;
    xxwidgets_die_signature_debug_config config;
    char *source, *source_name, *entry_function;
    debug_breakpoint *breakpoints;
    size_t breakpoint_count;
    char **watches;
    size_t watch_count;
    int cancel, pause_requested, refresh_requested;
    int step; /* 0 continue, 1 into, 2 over, 3 out. */
    int step_depth;
    size_t selected_frame;
    xxwidgets_die_signature_debug_snapshot view;
};

static char *debug_copy(const char *text)
{
    size_t n = text ? strlen(text) : 0;
    char *copy = (char *)malloc(n + 1);
    if (copy) { if (n) memcpy(copy, text, n); copy[n] = 0; }
    return copy;
}
static char *debug_source_copy(const char *source)
{
    size_t i, used = 0, n = strlen(source);
    char *copy = (char *)malloc(n + 1);
    if (!copy) return NULL;
    for (i = 0; i < n; ++i) {
        if (source[i] == '\r') { copy[used++] = '\n'; if (i + 1 < n && source[i + 1] == '\n') ++i; }
        else copy[used++] = source[i];
    }
    copy[used] = 0; return copy;
}
/* Preserve valid UTF-8, escape byte strings/control codes for safe GUI/TUI output. */
static size_t debug_utf8_unit(const unsigned char *p, size_t remaining)
{
    unsigned int c; size_t n, i;
    if (!remaining) return 0;
    if (p[0] < 0x80) return 1;
    if (p[0] >= 0xC2 && p[0] <= 0xDF) { n = 2; c = p[0] & 31u; }
    else if (p[0] >= 0xE0 && p[0] <= 0xEF) { n = 3; c = p[0] & 15u; }
    else if (p[0] >= 0xF0 && p[0] <= 0xF4) { n = 4; c = p[0] & 7u; }
    else return 0;
    if (remaining < n) return 0;
    for (i = 1; i < n; ++i) { if ((p[i] & 0xC0) != 0x80) return 0; c = (c << 6) | (p[i] & 63u); }
    if ((n == 3 && c < 0x800) || (n == 4 && c < 0x10000) || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0;
    return n;
}
static char *debug_display_text(const char *text, size_t limit, int *truncated)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *data = (const unsigned char *)(text ? text : "");
    size_t n = strlen((const char *)data), i = 0, used = 0, capacity;
    char *out;
    capacity = n > limit / 4 ? limit : n * 4;
    out = (char *)malloc(capacity + 1);
    if (!out) return NULL;
    while (i < n) {
        size_t unit = debug_utf8_unit(data + i, n - i);
        int escape = !unit || (data[i] < 32 && data[i] != '\n' && data[i] != '\r' && data[i] != '\t') || data[i] == 127;
        size_t needed = escape ? 4 : unit;
        if (needed > capacity - used) break;
        if (escape) { out[used++] = '\\'; out[used++] = 'x'; out[used++] = hex[data[i] >> 4]; out[used++] = hex[data[i] & 15]; ++i; }
        else { memcpy(out + used, data + i, unit); used += unit; i += unit; }
    }
    if (i < n && truncated) *truncated = 1;
    out[used] = 0;
    return out;
}
static int debug_active(xxwidgets_die_signature_debug_state state)
{
    return state == XXWIDGETS_DIE_DEBUG_RUNNING || state == XXWIDGETS_DIE_DEBUG_PAUSED || state == XXWIDGETS_DIE_DEBUG_STOPPING;
}
static void debug_free_values(xxwidgets_die_signature_debug_value *values, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) { free(values[i].name); free(values[i].value); }
    free(values);
}
static void debug_free_frames(xxwidgets_die_signature_debug_frame *frames, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) { free(frames[i].source_name); free(frames[i].function_name); }
    free(frames);
}
void xxwidgets_die_signature_debug_snapshot_free(xxwidgets_die_signature_debug_snapshot *view)
{
    if (!view) return;
    free(view->source_name); free(view->function_name);
    debug_free_frames(view->frames, view->frame_count);
    debug_free_values(view->locals, view->local_count);
    debug_free_values(view->watches, view->watch_count);
    free(view->log); free(view->error); free(view->result);
    memset(view, 0, sizeof(*view));
}
static void debug_clear_inspection(xxwidgets_die_signature_debug_snapshot *view)
{
    free(view->source_name); view->source_name = NULL;
    free(view->function_name); view->function_name = NULL;
    debug_free_frames(view->frames, view->frame_count); view->frames = NULL; view->frame_count = 0;
    debug_free_values(view->locals, view->local_count); view->locals = NULL; view->local_count = 0;
    debug_free_values(view->watches, view->watch_count); view->watches = NULL; view->watch_count = 0;
    view->line = view->column = 0; view->exception = 0;
}
static char *debug_value_text(JSVal value, int *truncated)
{
    char number[96];
    const char *simple = NULL;
    switch (value.tag) {
    case JT_UNDEF: simple = "undefined"; break;
    case JT_NULL: simple = "null"; break;
    case JT_BOOL: simple = value.u.b ? "true" : "false"; break;
    case JT_NUM: snprintf(number, sizeof(number), "%.17g", value.u.n); simple = number; break;
    case JT_OBJ: simple = js_is_callable(value) ? "[Function]" : "[Object]"; break;
    case JT_STR: break;
    default: simple = "<unknown>"; break;
    }
    if (simple) return debug_copy(simple);
    {
        static const char hex[] = "0123456789ABCDEF";
        const unsigned char *data = (const unsigned char *)js_str_data(value);
        size_t i, n = js_str_len(value), used = 0;
        char *out = (char *)malloc(DEBUG_MAX_VALUE_TEXT + 1);
        if (!out) return NULL;
        out[used++] = '"';
        for (i = 0; i < n && used + 6 < DEBUG_MAX_VALUE_TEXT; ++i) {
            unsigned char c = data[i];
            if (c == '"' || c == '\\') { out[used++] = '\\'; out[used++] = (char)c; }
            else if (c == '\n' || c == '\r' || c == '\t') {
                out[used++] = '\\'; out[used++] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't';
            } else if (c < 32 || c >= 127) {
                out[used++] = '\\'; out[used++] = 'x'; out[used++] = hex[c >> 4]; out[used++] = hex[c & 15];
            } else out[used++] = (char)c;
        }
        if (i < n) { out[used++] = '.'; out[used++] = '.'; out[used++] = '.'; if (truncated) *truncated = 1; }
        out[used++] = '"'; out[used] = 0;
        return out;
    }
}
static int debug_identifier(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    if (!p || !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_' || *p == '$')) return 0;
    for (++p; *p; ++p) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '$')) return 0;
    return 1;
}
static int debug_capture(xxwidgets_die_signature_debug_session *s, JSCtx *ctx, const JSDebugEvent *event)
{
    size_t i, count;
    xxwidgets_die_signature_debug_snapshot *v = &s->view;
    debug_clear_inspection(v);
    v->source_name = debug_copy(event->pSourceName);
    v->function_name = debug_copy(event->pFunctionName);
    if (!v->source_name || !v->function_name) return 0;
    v->line = event->nLine > 0 ? (size_t)event->nLine : 0;
    v->column = event->nColumn > 0 ? (size_t)event->nColumn : 0;
    v->exception = event->type == JS_DEBUG_EXCEPTION;
    if (!v->exception) { free(v->error); v->error = NULL; }
    count = js_debug_frame_count(ctx);
    if (count > DEBUG_MAX_FRAMES) { count = DEBUG_MAX_FRAMES; v->truncated = 1; }
    if (count) {
        v->frames = (xxwidgets_die_signature_debug_frame *)calloc(count, sizeof(*v->frames));
        if (!v->frames) return 0;
        v->frame_count = count;
        for (i = 0; i < count; ++i) {
            JSDebugFrameInfo frame;
            if (!js_debug_frame(ctx, i, &frame)) continue;
            v->frames[i].source_name = debug_copy(frame.pSourceName);
            v->frames[i].function_name = debug_copy(frame.pFunctionName);
            v->frames[i].line = frame.nLine > 0 ? (size_t)frame.nLine : 0;
            v->frames[i].column = frame.nColumn > 0 ? (size_t)frame.nColumn : 0;
            v->frames[i].native = frame.bNative;
            if (!v->frames[i].source_name || !v->frames[i].function_name) return 0;
        }
    }
    if (s->selected_frame >= count) s->selected_frame = 0;
    v->selected_frame = s->selected_frame;
    count = js_debug_local_count(ctx, s->selected_frame);
    if (count > DEBUG_MAX_VALUES) { count = DEBUG_MAX_VALUES; v->truncated = 1; }
    if (count) {
        v->locals = (xxwidgets_die_signature_debug_value *)calloc(count, sizeof(*v->locals));
        if (!v->locals) return 0;
        v->local_count = count;
        for (i = 0; i < count; ++i) {
            const char *name = ""; JSVal value = js_undefined();
            int found = js_debug_local(ctx, s->selected_frame, i, &name, &value);
            v->locals[i].name = debug_copy(name);
            v->locals[i].available = found;
            v->locals[i].value = found ? debug_value_text(value, &v->truncated) : debug_copy("<unavailable>");
            if (found) js_release(ctx, value);
            if (!v->locals[i].name || !v->locals[i].value) return 0;
        }
    }
    count = s->watch_count;
    if (count) {
        v->watches = (xxwidgets_die_signature_debug_value *)calloc(count, sizeof(*v->watches));
        if (!v->watches) return 0;
        v->watch_count = count;
        for (i = 0; i < count; ++i) {
            JSVal value = js_undefined();
            int found = js_debug_lookup(ctx, s->selected_frame, s->watches[i], &value);
            v->watches[i].name = debug_copy(s->watches[i]);
            v->watches[i].available = found;
            v->watches[i].value = found ? debug_value_text(value, &v->truncated) : debug_copy("<unavailable>");
            if (found) js_release(ctx, value);
            if (!v->watches[i].name || !v->watches[i].value) return 0;
        }
    }
    if (v->exception) {
        char *error = debug_display_text(js_error(ctx), DEBUG_MAX_LOG, &v->truncated);
        if (!error) return 0;
        free(v->error); v->error = error;
    }
    return 1;
}
static int debug_hook(JSCtx *ctx, const JSDebugEvent *event, void *user)
{
    xxwidgets_die_signature_debug_session *s = (xxwidgets_die_signature_debug_session *)user;
    size_t i; int pause;
    xx_mutex_lock(s->mutex);
    if (s->cancel) { xx_mutex_unlock(s->mutex); return 0; }
    pause = s->pause_requested || event->type == JS_DEBUG_DEBUGGER ||
        (event->type == JS_DEBUG_EXCEPTION && s->config.pause_on_exception);
    if (s->step == 1 || (s->step == 2 && event->nDepth <= s->step_depth) ||
        (s->step == 3 && event->nDepth < s->step_depth)) pause = 1;
    for (i = 0; !pause && i < s->breakpoint_count; ++i) {
        const char *name = *s->breakpoints[i].source_name ? s->breakpoints[i].source_name : s->source_name;
        if (event->nLine > 0 && (size_t)event->nLine == s->breakpoints[i].line &&
            !strcmp(name, event->pSourceName ? event->pSourceName : "")) pause = 1;
    }
    if (!pause) { xx_mutex_unlock(s->mutex); return 1; }
    s->pause_requested = 0; s->step = 0;
    s->view.state = XXWIDGETS_DIE_DEBUG_PAUSED;
    s->step_depth = event->nDepth;
    s->selected_frame = 0; s->refresh_requested = 1;
    while (!s->cancel && s->view.state == XXWIDGETS_DIE_DEBUG_PAUSED) {
        if (s->refresh_requested) {
            s->refresh_requested = 0;
            if (!debug_capture(s, ctx, event)) {
                free(s->view.error); s->view.error = debug_copy("Cannot allocate debugger snapshot");
                s->cancel = 1; break;
            }
            ++s->view.revision;
        }
        if (xx_cond_wait(s->cond, s->mutex, XX_WAIT_INFINITE) == XX_WAIT_ERROR) { s->cancel = 1; break; }
    }
    pause = !s->cancel;
    xx_mutex_unlock(s->mutex);
    return pause;
}
static JSVal debug_print(JSCtx *ctx, JSVal this_value, int argc, JSVal *argv, void *user)
{
    xxwidgets_die_signature_debug_session *s = (xxwidgets_die_signature_debug_session *)user;
    int i; (void)ctx; (void)this_value;
    for (i = 0; i < argc; ++i) {
        char *text = debug_value_text(argv[i], NULL);
        if (i) xxwidgets_die_signature_debug_session_log(s, " ");
        if (text) { xxwidgets_die_signature_debug_session_log(s, text); free(text); }
    }
    xxwidgets_die_signature_debug_session_log(s, "\n");
    return js_undefined();
}
static int debug_worker(void *user)
{
    xxwidgets_die_signature_debug_session *s = (xxwidgets_die_signature_debug_session *)user;
    JSCtx *ctx = js_new();
    JSVal value = js_undefined();
    char *result = NULL, *error = NULL;
    int success = 0;
    if (ctx) {
        JSVal console, global = js_global(ctx);
        js_debug_set_hook(ctx, debug_hook, s);
        js_def_fn(ctx, "print", debug_print, 0, s);
        console = js_new_object(ctx);
        js_def_method(ctx, console, "log", debug_print, 0, s);
        /* js_set consumes its value; js_global returns an owned reference. */
        js_set(ctx, global, "console", console);
        if (!s->config.setup || s->config.setup(ctx, s->config.user_data)) {
            if (s->config.execute) success = s->config.execute(ctx, s->source, s->source_name, &result, s->config.user_data);
            else {
                success = js_eval(ctx, s->source, s->source_name, &value);
                if (success && s->entry_function && *s->entry_function) {
                    JSVal fn = js_get(ctx, global, s->entry_function);
                    js_release(ctx, value); value = js_undefined();
                    if (!js_is_callable(fn)) {
                        js_throw(ctx, "Signature entry function '%s' is not callable", s->entry_function); success = 0;
                    } else {
                        JSVal args[3]; args[0] = args[1] = args[2] = js_bool(1);
                        value = js_call(ctx, fn, global, 3, args);
                        success = !js_has_exception(ctx);
                    }
                    js_release(ctx, fn);
                }
                if (success) result = debug_value_text(value, NULL);
            }
        }
        if (!success) error = debug_copy(result && *result ? result : js_error(ctx));
        js_release(ctx, value);
        if (s->config.cleanup) s->config.cleanup(ctx, s->config.user_data);
        js_debug_set_hook(ctx, NULL, NULL); js_release(ctx, global); js_free(ctx);
    } else error = debug_copy("Cannot create JavaScript context");
    xx_mutex_lock(s->mutex);
    free(s->view.result); s->view.result = debug_display_text(result, DEBUG_MAX_LOG, &s->view.truncated);
    free(s->view.error); s->view.error = debug_display_text(error, DEBUG_MAX_LOG, &s->view.truncated);
    free(result); free(error);
    s->view.state = s->cancel ? XXWIDGETS_DIE_DEBUG_STOPPED : success ? XXWIDGETS_DIE_DEBUG_FINISHED : XXWIDGETS_DIE_DEBUG_ERROR;
    if (!success && !s->cancel && (!s->view.error || !*s->view.error)) {
        free(s->view.error); s->view.error = debug_copy("Signature execution failed");
    }
    ++s->view.revision;
    xx_cond_broadcast(s->cond);
    xx_mutex_unlock(s->mutex);
    return 0;
}
void xxwidgets_die_signature_debug_config_init(xxwidgets_die_signature_debug_config *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config)); config->pause_on_exception = 1; config->entry_function = "detect";
}
xxwidgets_status xxwidgets_die_signature_debug_session_create(const xxwidgets_die_signature_debug_config *config,
    xxwidgets_die_signature_debug_session **out)
{
    xxwidgets_die_signature_debug_session *s;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL; s = (xxwidgets_die_signature_debug_session *)calloc(1, sizeof(*s));
    if (!s) return XXWIDGETS_OUT_OF_MEMORY;
    xxwidgets_die_signature_debug_config_init(&s->config);
    if (config) s->config = *config;
    s->entry_function = debug_copy(s->config.entry_function);
    s->config.entry_function = s->entry_function;
    s->source = debug_copy(""); s->source_name = debug_copy("<signature>");
    s->mutex = xx_mutex_create(); s->cond = xx_cond_create();
    if (!s->entry_function || !s->source || !s->source_name || !s->mutex || !s->cond) {
        xxwidgets_die_signature_debug_session_destroy(s); return XXWIDGETS_OUT_OF_MEMORY;
    }
    *out = s; return XXWIDGETS_OK;
}
void xxwidgets_die_signature_debug_session_destroy(xxwidgets_die_signature_debug_session *s)
{
    size_t i;
    if (!s) return;
    if (s->mutex) {
        xx_mutex_lock(s->mutex); s->cancel = 1;
        if (s->cond) xx_cond_broadcast(s->cond);
        xx_mutex_unlock(s->mutex);
    }
    if (s->thread) xx_thread_join(s->thread, NULL);
    for (i = 0; i < s->breakpoint_count; ++i) free(s->breakpoints[i].source_name);
    for (i = 0; i < s->watch_count; ++i) free(s->watches[i]);
    free(s->breakpoints); free(s->watches);
    free(s->source); free(s->source_name); free(s->entry_function);
    xxwidgets_die_signature_debug_snapshot_free(&s->view);
    if (s->cond) xx_cond_destroy(s->cond);
    if (s->mutex) xx_mutex_destroy(s->mutex);
    free(s);
}
xxwidgets_status xxwidgets_die_signature_debug_session_set_source(xxwidgets_die_signature_debug_session *s, const char *source, const char *name)
{
    char *source_copy, *name_copy;
    if (!s || !source || strlen(source) > DEBUG_MAX_SOURCE) return XXWIDGETS_INVALID_ARGUMENT;
    source_copy = debug_source_copy(source); name_copy = debug_copy(name && *name ? name : "<signature>");
    if (!source_copy || !name_copy) { free(source_copy); free(name_copy); return XXWIDGETS_OUT_OF_MEMORY; }
    xx_mutex_lock(s->mutex);
    if (debug_active(s->view.state)) { xx_mutex_unlock(s->mutex); free(source_copy); free(name_copy); return XXWIDGETS_BUSY; }
    free(s->source); free(s->source_name); s->source = source_copy; s->source_name = name_copy;
    {
        uint64_t revision = s->view.revision;
        xxwidgets_die_signature_debug_snapshot_free(&s->view);
        s->view.state = XXWIDGETS_DIE_DEBUG_IDLE; s->view.revision = revision + 1;
    }
    xx_mutex_unlock(s->mutex); return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debug_session_source(xxwidgets_die_signature_debug_session *s, const char *name, char **out)
{
    char *copy;
    if (!s || !out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL; xx_mutex_lock(s->mutex);
    if (name && *name && strcmp(name, s->source_name)) { xx_mutex_unlock(s->mutex); return XXWIDGETS_UNAVAILABLE; }
    copy = debug_copy(s->source); xx_mutex_unlock(s->mutex);
    if (!copy) return XXWIDGETS_OUT_OF_MEMORY;
    *out = copy; return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debug_session_set_breakpoints(xxwidgets_die_signature_debug_session *s,
    const xxwidgets_die_signature_debug_breakpoint *points, size_t count)
{
    debug_breakpoint *copy = NULL; size_t i;
    if (!s || (count && !points) || count > DEBUG_MAX_BREAKPOINTS) return XXWIDGETS_INVALID_ARGUMENT;
    if (count) { copy = (debug_breakpoint *)calloc(count, sizeof(*copy)); if (!copy) return XXWIDGETS_OUT_OF_MEMORY; }
    for (i = 0; i < count; ++i) {
        if (!points[i].line || points[i].line > INT_MAX) break;
        copy[i].line = points[i].line; copy[i].source_name = debug_copy(points[i].source_name);
        if (!copy[i].source_name) break;
    }
    if (i != count) {
        size_t j; for (j = 0; j < i; ++j) free(copy[j].source_name); free(copy);
        return points[i].line && points[i].line <= INT_MAX ? XXWIDGETS_OUT_OF_MEMORY : XXWIDGETS_INVALID_ARGUMENT;
    }
    xx_mutex_lock(s->mutex);
    for (i = 0; i < s->breakpoint_count; ++i) free(s->breakpoints[i].source_name);
    free(s->breakpoints); s->breakpoints = copy; s->breakpoint_count = count; ++s->view.revision;
    xx_mutex_unlock(s->mutex); return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debug_session_set_watches(xxwidgets_die_signature_debug_session *s, const char *const *names, size_t count)
{
    char **copy = NULL; size_t i;
    if (!s || (count && !names) || count > DEBUG_MAX_WATCHES) return XXWIDGETS_INVALID_ARGUMENT;
    if (count) { copy = (char **)calloc(count, sizeof(*copy)); if (!copy) return XXWIDGETS_OUT_OF_MEMORY; }
    for (i = 0; i < count; ++i) { if (!debug_identifier(names[i]) || strlen(names[i]) > 1024) break; copy[i] = debug_copy(names[i]); if (!copy[i]) break; }
    if (i != count) {
        size_t j; for (j = 0; j < i; ++j) free(copy[j]); free(copy);
        return debug_identifier(names[i]) && strlen(names[i]) <= 1024 ? XXWIDGETS_OUT_OF_MEMORY : XXWIDGETS_INVALID_ARGUMENT;
    }
    xx_mutex_lock(s->mutex);
    for (i = 0; i < s->watch_count; ++i) free(s->watches[i]);
    free(s->watches); s->watches = copy; s->watch_count = count;
    if (s->view.state == XXWIDGETS_DIE_DEBUG_PAUSED) { s->refresh_requested = 1; xx_cond_signal(s->cond); }
    ++s->view.revision; xx_mutex_unlock(s->mutex); return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debug_session_select_frame(xxwidgets_die_signature_debug_session *s, size_t frame)
{
    if (!s) return XXWIDGETS_INVALID_ARGUMENT;
    xx_mutex_lock(s->mutex);
    if (s->view.state != XXWIDGETS_DIE_DEBUG_PAUSED) { xx_mutex_unlock(s->mutex); return XXWIDGETS_BUSY; }
    if (frame >= s->view.frame_count) { xx_mutex_unlock(s->mutex); return XXWIDGETS_INVALID_ARGUMENT; }
    s->selected_frame = frame; s->refresh_requested = 1; xx_cond_signal(s->cond);
    xx_mutex_unlock(s->mutex); return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debug_session_start(xxwidgets_die_signature_debug_session *s, int pause_on_entry)
{
    xx_thread_t *previous;
    if (!s) return XXWIDGETS_INVALID_ARGUMENT;
    xx_mutex_lock(s->mutex);
    if (debug_active(s->view.state)) { xx_mutex_unlock(s->mutex); return XXWIDGETS_BUSY; }
    previous = s->thread; s->thread = NULL;
    xx_mutex_unlock(s->mutex);
    if (previous) xx_thread_join(previous, NULL);
    xx_mutex_lock(s->mutex);
    {
        uint64_t revision = s->view.revision;
        xxwidgets_die_signature_debug_snapshot_free(&s->view);
        s->view.revision = revision + 1;
    }
    s->cancel = 0; s->pause_requested = !!pause_on_entry; s->refresh_requested = 0;
    s->step = 0; s->step_depth = 0; s->selected_frame = 0; s->view.state = XXWIDGETS_DIE_DEBUG_RUNNING;
    /* Worker takes this same mutex before publishing inspection/state. */
    s->thread = xx_thread_create(debug_worker, s);
    if (!s->thread) { s->view.state = XXWIDGETS_DIE_DEBUG_ERROR; s->view.error = debug_copy("Cannot create debugger worker"); ++s->view.revision; }
    xx_mutex_unlock(s->mutex);
    return s->thread ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
}
xxwidgets_status xxwidgets_die_signature_debug_session_command(xxwidgets_die_signature_debug_session *s, xxwidgets_die_signature_debug_command command)
{
    if (!s || command < XXWIDGETS_DIE_DEBUG_CONTINUE || command > XXWIDGETS_DIE_DEBUG_STOP) return XXWIDGETS_INVALID_ARGUMENT;
    xx_mutex_lock(s->mutex);
    if (!debug_active(s->view.state)) { xx_mutex_unlock(s->mutex); return XXWIDGETS_BUSY; }
    if (command == XXWIDGETS_DIE_DEBUG_STOP) { s->cancel = 1; s->view.state = XXWIDGETS_DIE_DEBUG_STOPPING; }
    else if (command == XXWIDGETS_DIE_DEBUG_PAUSE) s->pause_requested = 1;
    else {
        if (s->view.state != XXWIDGETS_DIE_DEBUG_PAUSED) { xx_mutex_unlock(s->mutex); return XXWIDGETS_BUSY; }
        s->step = command == XXWIDGETS_DIE_DEBUG_STEP_INTO ? 1 : command == XXWIDGETS_DIE_DEBUG_STEP_OVER ? 2 : command == XXWIDGETS_DIE_DEBUG_STEP_OUT ? 3 : 0;
        s->pause_requested = 0; s->view.state = XXWIDGETS_DIE_DEBUG_RUNNING;
    }
    ++s->view.revision; xx_cond_broadcast(s->cond); xx_mutex_unlock(s->mutex); return XXWIDGETS_OK;
}
static int debug_clone_values(const xxwidgets_die_signature_debug_value *src, size_t count, xxwidgets_die_signature_debug_value **out)
{
    size_t i; *out = NULL;
    if (!count) return 1;
    *out = (xxwidgets_die_signature_debug_value *)calloc(count, sizeof(**out));
    if (!*out) return 0;
    for (i = 0; i < count; ++i) { (*out)[i].available = src[i].available; (*out)[i].name = debug_copy(src[i].name); (*out)[i].value = debug_copy(src[i].value); if (!(*out)[i].name || !(*out)[i].value) return 0; }
    return 1;
}
xxwidgets_status xxwidgets_die_signature_debug_session_snapshot(xxwidgets_die_signature_debug_session *s, xxwidgets_die_signature_debug_snapshot *out)
{
    xxwidgets_die_signature_debug_snapshot copy; size_t i; int ok = 1;
    if (!s || !out) return XXWIDGETS_INVALID_ARGUMENT;
    memset(&copy, 0, sizeof(copy)); xx_mutex_lock(s->mutex);
    copy = s->view;
    copy.source_name = debug_copy(s->view.source_name); copy.function_name = debug_copy(s->view.function_name);
    copy.log = debug_copy(s->view.log); copy.error = debug_copy(s->view.error); copy.result = debug_copy(s->view.result);
    copy.frames = NULL; copy.locals = NULL; copy.watches = NULL;
    if (!copy.source_name || !copy.function_name || !copy.log || !copy.error || !copy.result) ok = 0;
    if (copy.frame_count) {
        copy.frames = (xxwidgets_die_signature_debug_frame *)calloc(copy.frame_count, sizeof(*copy.frames));
        if (!copy.frames) { copy.frame_count = 0; ok = 0; }
        else for (i = 0; i < copy.frame_count; ++i) {
            copy.frames[i] = s->view.frames[i]; copy.frames[i].source_name = debug_copy(s->view.frames[i].source_name); copy.frames[i].function_name = debug_copy(s->view.frames[i].function_name);
            if (!copy.frames[i].source_name || !copy.frames[i].function_name) ok = 0;
        }
    }
    if (!debug_clone_values(s->view.locals, copy.local_count, &copy.locals)) { if (!copy.locals) copy.local_count = 0; ok = 0; }
    if (!debug_clone_values(s->view.watches, copy.watch_count, &copy.watches)) { if (!copy.watches) copy.watch_count = 0; ok = 0; }
    xx_mutex_unlock(s->mutex);
    if (!ok) { xxwidgets_die_signature_debug_snapshot_free(&copy); return XXWIDGETS_OUT_OF_MEMORY; }
    *out = copy; return XXWIDGETS_OK;
}
void xxwidgets_die_signature_debug_session_log(xxwidgets_die_signature_debug_session *s, const char *text)
{
    size_t old_size, add; char *next, *display;
    if (!s || !text) return;
    xx_mutex_lock(s->mutex);
    old_size = s->view.log ? strlen(s->view.log) : 0;
    display = debug_display_text(text, DEBUG_MAX_LOG - old_size, &s->view.truncated);
    if (!display) { s->view.truncated = 1; xx_mutex_unlock(s->mutex); return; }
    add = strlen(display);
    if (add) {
        next = (char *)realloc(s->view.log, old_size + add + 1);
        if (next) { memcpy(next + old_size, display, add); next[old_size + add] = 0; s->view.log = next; ++s->view.revision; }
        else s->view.truncated = 1;
    }
    free(display); xx_mutex_unlock(s->mutex);
}
int xxwidgets_die_signature_debug_session_cancelled(xxwidgets_die_signature_debug_session *s)
{
    int cancel;
    if (!s) return 1;
    xx_mutex_lock(s->mutex); cancel = s->cancel; xx_mutex_unlock(s->mutex); return cancel;
}
