/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets/die_engine/xxwidgets_die_signature_debug_die_engine.h"
#include "xxfclib/die_engine/die_engine_debug.h"
#include "xxfclib/js/xx_js_debug.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdlib.h>
#include <string.h>

#define DEBUG_DIE_MAX_SOURCE (8u * 1024u * 1024u)
struct xxwidgets_die_signature_debug_die_engine {
    xxwidgets_die_signature_debug_session *session;
    DBase *database;
    char *input_path;
    XFileType file_type;
    xx_scan_options options;
};
static char *debug_die_copy(const char *text)
{
    char *copy; size_t n;
    if (!text) return NULL;
    n = strlen(text); copy = (char *)malloc(n + 1);
    if (copy) memcpy(copy, text, n + 1);
    return copy;
}
static int debug_die_copy_options(xx_scan_options *out, const xx_scan_options *in)
{
    if (in) *out = *in; else xx_scan_options_init(out);
    out->main_database_path = debug_die_copy(in ? in->main_database_path : NULL);
    out->extra_database_path = debug_die_copy(in ? in->extra_database_path : NULL);
    out->custom_database_path = debug_die_copy(in ? in->custom_database_path : NULL);
    out->file_name = debug_die_copy(in ? in->file_name : NULL);
    out->resource_type = debug_die_copy(in ? in->resource_type : NULL);
    out->archive_member = debug_die_copy(in ? in->archive_member : NULL);
    out->include_tags = debug_die_copy(in ? in->include_tags : NULL);
    out->exclude_tags = debug_die_copy(in ? in->exclude_tags : NULL);
    out->engine_options = NULL;
    /* One target, one explicit source, one debugger lifetime. */
    out->recursive_scan = out->overlay_scan = out->resources_scan = out->archives_scan = out->all_types_scan = false;
    out->first_wrapper_only = false;
    out->scan_id = out->parent_id = 0;
    if (!in) return 1;
    return (!in->main_database_path || out->main_database_path) && (!in->extra_database_path || out->extra_database_path) &&
        (!in->custom_database_path || out->custom_database_path) && (!in->file_name || out->file_name) &&
        (!in->resource_type || out->resource_type) && (!in->archive_member || out->archive_member) &&
        (!in->include_tags || out->include_tags) && (!in->exclude_tags || out->exclude_tags);
}
static void debug_die_free_options(xx_scan_options *options)
{
    free(options->main_database_path); free(options->extra_database_path); free(options->custom_database_path);
    free((char *)options->file_name); free((char *)options->resource_type); free((char *)options->archive_member);
    free((char *)options->include_tags); free((char *)options->exclude_tags);
    memset(options, 0, sizeof(*options));
}
static void debug_die_log(const char *text, void *user)
{
    xxwidgets_die_signature_debug_die_engine *a = (xxwidgets_die_signature_debug_die_engine *)user;
    xxwidgets_die_signature_debug_session_log(a->session, text);
}
static bool debug_die_progress(const xx_pd_struct *pd, void *user)
{
    xxwidgets_die_signature_debug_die_engine *a = (xxwidgets_die_signature_debug_die_engine *)user;
    (void)pd;
    return xxwidgets_die_signature_debug_session_cancelled(a->session) != 0;
}
static int debug_die_execute(JSCtx *ctx, const char *source, const char *source_name, char **text, void *user)
{
    xxwidgets_die_signature_debug_die_engine *a = (xxwidgets_die_signature_debug_die_engine *)user;
    xx_scan_result result;
    xx_scan_options options = a->options;
    xx_pd_struct progress = xx_pd_init();
    xx_pd_observer old_observer;
    die_engine_debug_config config;
    char *formatted; int ok, i;
    memset(&result, 0, sizeof(result)); memset(&config, 0, sizeof(config));
    config.size = sizeof(config); config.version = DIE_ENGINE_DEBUG_VERSION;
    js_debug_get_hook(ctx, &config.hook, &config.hook_user);
    config.log = debug_die_log; config.log_user = a;
    config.signature_source = source; config.signature_name = source_name;
    options.file_name = a->input_path;
    old_observer = xx_pd_set_observer(&progress, debug_die_progress, a);
    ok = die_engine_scan_file_type_debug_pd(a->input_path, a->database, &options, a->file_type, &result, &progress, &config);
    xx_pd_set_observer(old_observer.progress, old_observer.callback, old_observer.user_data);
    for (i = 0; i < result.nErrorCount; ++i) {
        debug_die_log(result.ppErrors[i], a); debug_die_log("\n", a);
    }
    if (ok && !progress.is_stop && !progress.last_error && !result.nErrorCount) {
        formatted = xx_scan_format_text(&result, &options);
        if (formatted) { *text = debug_die_copy(formatted); xx_rt_free(formatted); }
        else *text = debug_die_copy("Signature completed; no formatted result");
    } else {
        const char *error = progress.error_string[0] ? progress.error_string : result.nErrorCount > 0 ? result.ppErrors[0] : "DIE signature execution failed";
        *text = debug_die_copy(error); ok = 0;
    }
    xx_scan_result_free(&result);
    return ok && *text != NULL;
}
xxwidgets_status xxwidgets_die_signature_debug_die_engine_create(const xxwidgets_die_signature_debug_die_config *config,
    xxwidgets_die_signature_debug_die_engine **out)
{
    xxwidgets_die_signature_debug_die_engine *a;
    xxwidgets_die_signature_debug_config session_config;
    xxwidgets_status status;
    if (!out) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    if (!config || !config->input_path || !*config->input_path || !config->database || config->file_type < XFT_UNKNOWN || config->file_type >= XFT_COUNT)
        return XXWIDGETS_INVALID_ARGUMENT;
    a = (xxwidgets_die_signature_debug_die_engine *)calloc(1, sizeof(*a));
    if (!a) return XXWIDGETS_OUT_OF_MEMORY;
    a->input_path = debug_die_copy(config->input_path); a->database = config->database; a->file_type = config->file_type;
    if (!a->input_path || !debug_die_copy_options(&a->options, config->options)) {
        xxwidgets_die_signature_debug_die_engine_destroy(a); return XXWIDGETS_OUT_OF_MEMORY;
    }
    xxwidgets_die_signature_debug_config_init(&session_config);
    session_config.execute = debug_die_execute; session_config.user_data = a;
    status = xxwidgets_die_signature_debug_session_create(&session_config, &a->session);
    if (status != XXWIDGETS_OK) { xxwidgets_die_signature_debug_die_engine_destroy(a); return status; }
    *out = a; return XXWIDGETS_OK;
}
void xxwidgets_die_signature_debug_die_engine_destroy(xxwidgets_die_signature_debug_die_engine *a)
{
    if (!a) return;
    xxwidgets_die_signature_debug_session_destroy(a->session);
    debug_die_free_options(&a->options); free(a->input_path); free(a);
}
xxwidgets_die_signature_debug_session *xxwidgets_die_signature_debug_die_engine_session(xxwidgets_die_signature_debug_die_engine *a)
{
    return a ? a->session : NULL;
}
xxwidgets_status xxwidgets_die_signature_debug_die_engine_source(xxwidgets_die_signature_debug_die_engine *a, const char *name, char **out)
{
    int i;
    xxwidgets_status status;
    if (!a || !name || !out) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_die_signature_debug_session_source(a->session, name, out);
    if (status != XXWIDGETS_UNAVAILABLE) return status;
    *out = NULL;
    for (i = 0; i < a->database->nCount; ++i) {
        DBSignature *record = &a->database->pRecords[i];
        if ((record->pName && !strcmp(record->pName, name)) || (record->pFilePath && !strcmp(record->pFilePath, name))) {
            char *source;
            if (!record->pText) return XXWIDGETS_UNAVAILABLE;
            if (js_is_bytecode(record->pText, record->nSize)) {
                /* Decompilation changes layout; never attach original event
                 * lines to a differently formatted helper document. */
                return XXWIDGETS_UNAVAILABLE;
            } else {
                size_t n = record->nSize;
                if (n && !record->pText[n - 1]) --n;
                if (n > DEBUG_DIE_MAX_SOURCE || memchr(record->pText, 0, n)) return XXWIDGETS_UNAVAILABLE;
                source = (char *)malloc(n + 1);
                if (source) { memcpy(source, record->pText, n); source[n] = 0; }
            }
            if (!source) return XXWIDGETS_OUT_OF_MEMORY;
            *out = source; return XXWIDGETS_OK;
        }
    }
    return XXWIDGETS_UNAVAILABLE;
}
