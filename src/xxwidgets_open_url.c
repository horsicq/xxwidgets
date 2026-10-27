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

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "xxwidgets_internal.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Only web and mail links, already percent-encoded: nothing a shell or the
 * handler could read as an option, a path or a second argument. */
static int url_allowed(const char *url)
{
    const unsigned char *s = (const unsigned char *)url;
    if (strncmp(url, "https://", 8) && strncmp(url, "http://", 7) && strncmp(url, "mailto:", 7)) return 0;
    for (; *s; ++s)
        if (*s <= 32 || *s >= 127 || *s == '"' || *s == '\\' || *s == '`') return 0;
    return 1;
}

xxwidgets_status xxwidgets_open_url(xxwidgets_widget *owner, const char *url)
{
    if (!owner || !url || !url_allowed(url)) return XXWIDGETS_INVALID_ARGUMENT;
#ifdef _WIN32
    {
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, NULL, 0);
        wchar_t *wide;
        HINSTANCE result;
        if (length <= 0) return XXWIDGETS_INVALID_ARGUMENT;
        wide = (wchar_t *)malloc((size_t)length * sizeof(*wide));
        if (!wide) return XXWIDGETS_OUT_OF_MEMORY;
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, wide, length);
        result = ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
        free(wide);
        return (INT_PTR)result > 32 ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
    }
#else
    {
#ifdef __APPLE__
        const char *tool = "open";
#else
        const char *tool = "xdg-open";
        /* A terminal without a desktop session has no browser to hand it to. */
        if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) return XXWIDGETS_UNAVAILABLE;
#endif
        int report[2], code = 0, status;
        ssize_t got;
        pid_t child;
        if (pipe(report)) return XXWIDGETS_PLATFORM_ERROR;
        (void)fcntl(report[1], F_SETFD, FD_CLOEXEC);
        child = fork();
        if (child < 0) {
            close(report[0]); close(report[1]);
            return XXWIDGETS_PLATFORM_ERROR;
        }
        if (!child) {
            /* The handler outlives this call: a grandchild nobody waits for,
             * reparented when the middle process exits at once. */
            if (fork() == 0) {
                int null = open("/dev/null", O_RDWR);
                close(report[0]);
                if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); if (null > 2) close(null); }
                setsid();
                execlp(tool, tool, url, (char *)NULL);
                code = errno ? errno : ENOENT;
                (void)!write(report[1], &code, sizeof(code));
                _exit(127);
            }
            _exit(0);
        }
        close(report[1]);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        /* EOF: exec succeeded and closed the descriptor. */
        do got = read(report[0], &code, sizeof(code)); while (got < 0 && errno == EINTR);
        close(report[0]);
        if (got > 0) return code == ENOENT ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_PLATFORM_ERROR;
        return XXWIDGETS_OK;
    }
#endif
}
