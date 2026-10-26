/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_DIE_SIGNATURE_DEBUG_DIE_ENGINE_H
#define XXWIDGETS_DIE_SIGNATURE_DEBUG_DIE_ENGINE_H
#include "xxwidgets_die_signature_debug_session.h"
#include "xxfclib/die_engine/die_engine.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_die_signature_debug_die_engine xxwidgets_die_signature_debug_die_engine;
typedef struct xxwidgets_die_signature_debug_die_config {
    const char *input_path;
    DBase *database;
    XFileType file_type;
    const xx_scan_options *options;
} xxwidgets_die_signature_debug_die_config;

/* Concrete DIE adapter: source/detect run against the actual input parsers and
 * native signature APIs. The complete database supplies _init/includeScript;
 * only the session's selected source is used as a detection rule. Input/options
 * strings are copied. The database is borrowed and must remain loaded and must
 * not be modified/freed until destroy joins the worker. NULL options selects
 * initialized defaults. Debug sessions disable child/recursive/parent scans. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_die_engine_create(
    const xxwidgets_die_signature_debug_die_config *config, xxwidgets_die_signature_debug_die_engine **out_adapter);
/* Stops/joins the owned session. Detach it from a debugger widget first. */
XXWIDGETS_API void xxwidgets_die_signature_debug_die_engine_destroy(xxwidgets_die_signature_debug_die_engine *adapter);
XXWIDGETS_API xxwidgets_die_signature_debug_session *xxwidgets_die_signature_debug_die_engine_session(
    xxwidgets_die_signature_debug_die_engine *adapter);
/* Source resolver for the debugger widget: exact primary session name, helper
 * pName or absolute pFilePath. Bytecode helpers return UNAVAILABLE because
 * decompilation changes source layout; a host resolver may provide the exact
 * original. Returned source is malloc-allocated; caller frees it. No JS executes. */
XXWIDGETS_API xxwidgets_status xxwidgets_die_signature_debug_die_engine_source(
    xxwidgets_die_signature_debug_die_engine *adapter, const char *source_name, char **out_source);

#ifdef __cplusplus
}
#endif
#endif
