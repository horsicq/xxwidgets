/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXWIDGETS_FORMAT_PANEL_H
#define XXWIDGETS_FORMAT_PANEL_H

#include "xxwidgets/xxwidgets.h"
#include "xxwidgets/xxwidgets_value_field.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xxwidgets_format_panel xxwidgets_format_panel;

typedef enum xxwidgets_format_page {
    XXWIDGETS_FORMAT_PAGE_BINARY = 0, XXWIDGETS_FORMAT_PAGE_COM, XXWIDGETS_FORMAT_PAGE_ARCHIVE,
    XXWIDGETS_FORMAT_PAGE_MSDOS, XXWIDGETS_FORMAT_PAGE_NE, XXWIDGETS_FORMAT_PAGE_LE,
    XXWIDGETS_FORMAT_PAGE_DEX, XXWIDGETS_FORMAT_PAGE_PE, XXWIDGETS_FORMAT_PAGE_ELF,
    XXWIDGETS_FORMAT_PAGE_MACH, XXWIDGETS_FORMAT_PAGE_COUNT
} xxwidgets_format_page;

typedef enum xxwidgets_format_item {
    XXWIDGETS_FORMAT_ITEM_MAIN = 0,
    XXWIDGETS_FORMAT_ITEM_PE_EXPORT, XXWIDGETS_FORMAT_ITEM_PE_IMPORT, XXWIDGETS_FORMAT_ITEM_PE_RESOURCES,
    XXWIDGETS_FORMAT_ITEM_PE_DOTNET, XXWIDGETS_FORMAT_ITEM_PE_TLS, XXWIDGETS_FORMAT_ITEM_PE_OVERLAY,
    XXWIDGETS_FORMAT_ITEM_PE_SECTIONS, XXWIDGETS_FORMAT_ITEM_PE_TIME_DATE_STAMP,
    XXWIDGETS_FORMAT_ITEM_PE_SIZE_OF_IMAGE, XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP,
    XXWIDGETS_FORMAT_ITEM_PE_MANIFEST, XXWIDGETS_FORMAT_ITEM_PE_VERSION,
    XXWIDGETS_FORMAT_ITEM_ELF_PROGRAMS, XXWIDGETS_FORMAT_ITEM_ELF_SECTIONS,
    XXWIDGETS_FORMAT_ITEM_MACH_COMMANDS, XXWIDGETS_FORMAT_ITEM_MACH_SEGMENTS,
    XXWIDGETS_FORMAT_ITEM_MACH_SECTIONS, XXWIDGETS_FORMAT_ITEM_MACH_LIBRARIES,
    XXWIDGETS_FORMAT_ITEM_MSDOS_OVERLAY,
    XXWIDGETS_FORMAT_ITEM_ARCHIVE_MANIFEST_MF, XXWIDGETS_FORMAT_ITEM_ARCHIVE_ANDROID_MANIFEST,
    XXWIDGETS_FORMAT_ITEM_ARCHIVE_FILES, XXWIDGETS_FORMAT_ITEM_ARCHIVE_UNPACK,
    XXWIDGETS_FORMAT_ITEM_EXTRA_IMPORT, XXWIDGETS_FORMAT_ITEM_EXTRA_EXPORT, XXWIDGETS_FORMAT_ITEM_EXTRA_SYMBOLS,
    XXWIDGETS_FORMAT_ITEM_COUNT
} xxwidgets_format_item;

XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_create(xxwidgets_widget *owner, unsigned int flags, xxwidgets_format_panel **out);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_destroy(xxwidgets_format_panel *panel);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_bounds(xxwidgets_format_panel *panel, xxwidgets_rect units);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_get_preferred_size(xxwidgets_format_panel *panel, int *width, int *height);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_get_minimum_size(xxwidgets_format_panel *panel, int *width, int *height);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_visible(xxwidgets_format_panel *panel, int visible);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_enabled_all(xxwidgets_format_panel *panel, int enabled);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_page(xxwidgets_format_panel *panel, xxwidgets_format_page page,
                                                               const char *main_label);
XXWIDGETS_API xxwidgets_format_page xxwidgets_format_panel_get_page(const xxwidgets_format_panel *panel);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_item_enabled(xxwidgets_format_panel *panel, xxwidgets_format_item item, int enabled);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_item_shown(xxwidgets_format_panel *panel, xxwidgets_format_item item, int shown);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_item_hex(xxwidgets_format_panel *panel, xxwidgets_format_item item,
                                                                  uint64_t value, unsigned int digits);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_item_text(xxwidgets_format_panel *panel, xxwidgets_format_item item,
                                                                   const char *utf8);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_set_item_caption(xxwidgets_format_panel *panel, xxwidgets_format_item item,
                                                                      const char *caption);
XXWIDGETS_API xxwidgets_widget *xxwidgets_format_panel_item_widget(const xxwidgets_format_panel *panel, xxwidgets_format_item item);
XXWIDGETS_API xxwidgets_value_field *xxwidgets_format_panel_item_field(const xxwidgets_format_panel *panel, xxwidgets_format_item item);
XXWIDGETS_API xxwidgets_widget *xxwidgets_format_panel_frame(const xxwidgets_format_panel *panel);
XXWIDGETS_API xxwidgets_status xxwidgets_format_panel_retranslate(xxwidgets_format_panel *panel);
XXWIDGETS_API int xxwidgets_format_panel_handle_event(xxwidgets_format_panel *panel, const xxwidgets_event *event,
                                                      xxwidgets_format_item *item, int *is_arrow);

#ifdef __cplusplus
}
#endif
#endif
