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

#ifndef XXWIDGETS_SHORTCUTS_DIALOG_H
#define XXWIDGETS_SHORTCUTS_DIALOG_H

#include "xxwidgets/xxwidgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One row of the read-only Shortcuts dialog. */
typedef struct xxwidgets_shortcut_entry {
    const char *name; /* action name, already translated, UTF-8 */
    const char *key;  /* key sequence as shown, e.g. "Ctrl+O" ("" for none) */
} xxwidgets_shortcut_entry;

/* Modal "Shortcuts" dialog: a "Filter" box over a Name | Shortcut table (the
 * TABLE kind) and Close. Typing in Filter keeps the rows whose name or key
 * contains it (case-insensitive). Read-only: editing and saving come later.
 * Entries are borrowed for the call; 0..4096 of them, valid UTF-8. Owner,
 * modality and reentrancy rules are those of xxwidgets_text_dialog. */
XXWIDGETS_API xxwidgets_status xxwidgets_shortcuts_dialog(xxwidgets_widget *owner, const xxwidgets_shortcut_entry *entries, size_t count);

#ifdef __cplusplus
}
#endif
#endif
