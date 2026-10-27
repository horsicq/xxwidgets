/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxwidgets_internal.h"

static int busy(const xxwidgets_app *app)
{
    return app->dispatch_depth || app->polling || app->syncing || app->modal_window;
}
static xxwidgets_status replace(xxwidgets_app *app, xx_translation *catalog,
    xx_translation_status status)
{
    if (status != XX_TRANSLATION_OK) {
        return status == XX_TRANSLATION_OUT_OF_MEMORY ? XXWIDGETS_OUT_OF_MEMORY :
            status == XX_TRANSLATION_IO_ERROR ? XXWIDGETS_PLATFORM_ERROR : XXWIDGETS_INVALID_ARGUMENT;
    }
    xx_translation_destroy(app->translation);
    app->translation = catalog;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_app_load_po(xxwidgets_app *app, const void *data,
    size_t size, size_t *error_line)
{
    xx_translation *catalog = NULL;
    xx_translation_status status;
    if (error_line) *error_line = 0;
    if (!app) return XXWIDGETS_INVALID_ARGUMENT;
    if (busy(app)) return XXWIDGETS_BUSY;
    status = xx_translation_load_po(data, size, &catalog, error_line);
    return replace(app, catalog, status);
}
xxwidgets_status xxwidgets_app_load_po_file(xxwidgets_app *app, const char *path,
    size_t *error_line)
{
    xx_translation *catalog = NULL;
    xx_translation_status status;
    if (error_line) *error_line = 0;
    if (!app) return XXWIDGETS_INVALID_ARGUMENT;
    if (busy(app)) return XXWIDGETS_BUSY;
    status = xx_translation_load_po_file(path, &catalog, error_line);
    return replace(app, catalog, status);
}
xxwidgets_status xxwidgets_app_clear_translation(xxwidgets_app *app)
{
    if (!app) return XXWIDGETS_INVALID_ARGUMENT;
    if (busy(app)) return XXWIDGETS_BUSY;
    return replace(app, NULL, XX_TRANSLATION_OK);
}
const char *xxwidgets_app_gettext(const xxwidgets_app *app, const char *msgid)
{
    return xx_translation_gettext(app ? app->translation : NULL, msgid);
}
const char *xxwidgets_app_pgettext(const xxwidgets_app *app, const char *context, const char *msgid)
{
    return xx_translation_pgettext(app ? app->translation : NULL, context, msgid);
}
const char *xxwidgets_app_ngettext(const xxwidgets_app *app, const char *singular,
    const char *plural, uint64_t n)
{
    return xx_translation_ngettext(app ? app->translation : NULL, singular, plural, n);
}

/* Internal typed-combobox adapter: translate labels without changing their
 * numeric values. set_records deep-copies the temporary Unicode strings. */
#include "xxwidgets/xxwidgets_combobox.h"
#include <stdlib.h>
#include <wchar.h>
xxwidgets_status xxwidgets_translated_choices(xxwidgets_widget *widget,
    const char *const *names, const uint64_t *values, size_t count)
{
    xx_meta_string *records;
    xx_str_w_s *labels;
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (!widget || (!names && count)) return XXWIDGETS_INVALID_ARGUMENT;
    if (!count) return xxwidgets_combobox_set_records(widget, NULL, 0);
    records = calloc(count, sizeof(*records));
    labels = calloc(count, sizeof(*labels));
    if (!records || !labels) { free(records); free(labels); return XXWIDGETS_OUT_OF_MEMORY; }
    for (i = 0; i < count; ++i) {
        labels[i].data = xx_str_utf8_to_unicode(XXW_TR(widget->app, names[i]));
        if (!labels[i].data) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
        labels[i].length = wcslen(labels[i].data);
        labels[i].capacity = labels[i].length + 1;
        labels[i].is_view = true;
        records[i].meta_string = labels + i;
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = values ? values[i] : i;
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_combobox_set_records(widget, records, count);
    for (i = 0; i < count; ++i) xx_str_free_unicode(labels[i].data);
    free(labels); free(records);
    return status;
}
