/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXWIDGETS_TRANSLATION_H
#define XXWIDGETS_TRANSLATION_H
#include "xxwidgets/xxwidgets.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The app owns its catalog. Load on the UI thread, preferably before creating
 * controls. Existing copied labels require recreation after a language change;
 * newly opened dialogs and menus use the new catalog. Loading is transactional.
 * INVALID_ARGUMENT includes malformed PO/unsupported charset (line is optional).
 * No catalog means English. Input bytes and path are not retained.
 */
XXWIDGETS_API xxwidgets_status xxwidgets_app_load_po(xxwidgets_app *app,
    const void *data, size_t size, size_t *error_line);
XXWIDGETS_API xxwidgets_status xxwidgets_app_load_po_file(xxwidgets_app *app,
    const char *path, size_t *error_line);
XXWIDGETS_API xxwidgets_status xxwidgets_app_clear_translation(xxwidgets_app *app);
/* Results are borrowed until successful catalog replacement, clear, or app
 * destruction. Missing/empty/fuzzy translations return the supplied English.
 * Application data is translated only when the application calls these APIs.
 */
XXWIDGETS_API const char *xxwidgets_app_gettext(const xxwidgets_app *app, const char *msgid);
XXWIDGETS_API const char *xxwidgets_app_pgettext(const xxwidgets_app *app, const char *context, const char *msgid);
XXWIDGETS_API const char *xxwidgets_app_ngettext(const xxwidgets_app *app,
    const char *singular, const char *plural, uint64_t n);
#ifdef __cplusplus
}
#endif
#endif
