#include "xxwidgets_internal.h"

#include <stdint.h>
#include <string.h>

/* The texts share a prefix and a suffix around one changed span. A caret in
 * the unchanged end stays before the same text. A caret in front of the span
 * stays where it is when the span is small next to the unchanged end (a '~'
 * expanded with the caret at 0), but not when the text was replaced. */
size_t xxwidgets_edit_caret(const char *old, size_t caret, const char *now)
{
    size_t old_length = strlen(old), now_length = strlen(now), prefix = 0, suffix = 0, shorter;
    if (caret > old_length) return SIZE_MAX;
    shorter = old_length < now_length ? old_length : now_length;
    while (prefix < shorter && old[prefix] == now[prefix]) ++prefix;
    while (prefix && ((unsigned char)now[prefix] & 0xc0) == 0x80) --prefix;
    while (suffix < shorter - prefix && old[old_length - 1 - suffix] == now[now_length - 1 - suffix]) ++suffix;
    while (suffix && ((unsigned char)old[old_length - suffix] & 0xc0) == 0x80) --suffix;
    if (caret >= old_length - suffix) return now_length - (old_length - caret);
    if (caret <= prefix && suffix > old_length - prefix - suffix) return caret;
    return SIZE_MAX;
}
