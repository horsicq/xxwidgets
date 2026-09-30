#include "demo_common.h"
#include "xxwidgets/xxwidgets_process.h"
#ifndef _WIN32
#include <time.h>
#endif

typedef struct process_demo {
    xxwidgets_widget *window, *fast, *slow, *quit, *status;
    int running, pending;
    uint64_t started, duration;
} process_demo;

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}

static xxwidgets_status update(void *user, int stop, xx_pd_struct *progress, int *finished)
{
    process_demo *demo = (process_demo *)user;
    const char *names[XX_PD_LEVELS] = {"Overall operation", "Reading a large file", "Processing records",
        "Work with unknown total", "Nested operation \xce\xbb"};
    uint64_t elapsed = now_ms() - demo->started;
    int i;
    *finished = stop || elapsed >= demo->duration;
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        xx_pd_record *record = &progress->records[i];
        record->is_busy = !*finished;
        record->total = i == 1 ? UINT64_C(5000000000) : demo->duration;
        record->current = (elapsed < demo->duration ? elapsed : demo->duration) * record->total / demo->duration;
        strcpy(record->status, names[i]);
    }
    /* Simulated work advances in short UI-thread steps. An actual worker must
     * supply a synchronized snapshot and a separate completion flag here. */
    progress->records[1].is_busy &= elapsed < demo->duration * 3 / 4;
    progress->records[2].is_busy &= elapsed < demo->duration / 2;
    progress->records[3].total = 0; progress->records[3].current = elapsed;
    progress->records[4].is_busy &= (elapsed / 700) % 2 == 0;
    return XXWIDGETS_OK;
}

static xxwidgets_status run_process(process_demo *demo, uint64_t duration, xx_pd_struct *progress)
{
    memset(progress, 0, sizeof(*progress));
    demo->duration = duration; demo->started = now_ms();
    return xxwidgets_process_dialog(demo->window, "Processing", progress, update, demo);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    process_demo *demo = (process_demo *)user;
    if (event->type == XXWIDGETS_EVENT_CLOSE ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == demo->quit)) {
        demo->running = 0; xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == demo->fast) demo->pending = 1;
        else if (event->widget == demo->slow) demo->pending = 2;
    }
}

int main(int argc, char **argv)
{
    process_demo demo = {0};
    demo_arguments arguments;
    xxwidgets_app *app = NULL;
    xxwidgets_config config = {XXWIDGETS_BACKEND_AUTO, on_event, &demo};
    xx_pd_struct progress = {0};
    int result = 0, parsed = demo_parse(argc, argv, "xxwidgets_process_demo", &arguments);
    if (parsed <= 0) return parsed < 0;
    config.backend = arguments.backend;
    if (!demo_check(xxwidgets_app_create(&config, &app))) return 1;
    demo.window = demo_widget(app, NULL, XXWIDGETS_WINDOW, "xxwidgets Process demo", 1, 1, 76, 22);
    if (!demo.window) return demo_finish(app, 1);
    if (arguments.smoke) xxwidgets_widget_set_visible(demo.window, 0);
    if (!demo_widget(app, demo.window, XXWIDGETS_LABEL, "A 200 ms job completes without opening a dialog.", 2, 2, 70, 1) ||
        !demo_widget(app, demo.window, XXWIDGETS_LABEL, "A 5 s job shows busy records after one second; inactive bars hide.", 2, 4, 70, 1) ||
        !demo_widget(app, demo.window, XXWIDGETS_LABEL, "Cancel, Escape and close request a cooperative stop.", 2, 6, 70, 1))
        return demo_finish(app, 1);
    demo.fast = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Fast job", 2, 9, 16, 2);
    demo.slow = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Slow job", 22, 9, 16, 2);
    demo.quit = demo_widget(app, demo.window, XXWIDGETS_BUTTON, "Quit", 42, 9, 12, 2);
    demo.status = demo_widget(app, demo.window, XXWIDGETS_LABEL, "Choose a job.", 2, 13, 70, 1);
    if (!demo.fast || !demo.slow || !demo.quit || !demo.status) return demo_finish(app, 1);
    if (arguments.smoke) {
        int dismissed;
        if (!demo_check(run_process(&demo, 200, &progress)) || progress.is_stop) return demo_finish(app, 1);
        if (!demo_modal_smoke_begin(app, demo.window, 0)) return demo_finish(app, 1);
        result = !demo_check(run_process(&demo, 1800, &progress));
        dismissed = demo_modal_smoke_finish();
        if (!dismissed || !progress.is_stop) {
            fprintf(stderr, "Process smoke: dismissed=%d, stop=%d\n", dismissed, progress.is_stop != 0);
            result = 1;
        }
    } else {
        demo.running = 1;
        xxwidgets_widget_focus(demo.fast);
        while (demo.running) {
            if (!demo_check(xxwidgets_app_poll(app, 30))) { result = 1; break; }
            if (demo.pending && demo.running) {
                uint64_t duration = demo.pending == 1 ? 200 : 5000;
                demo.pending = 0;
                /* Start the nested modal loop after the input callback returns. */
                if (!demo_check(run_process(&demo, duration, &progress)))
                    xxwidgets_widget_set_text(demo.status, "Cannot show progress.");
                else xxwidgets_widget_set_text(demo.status, progress.is_stop ? "Stopped." : "Completed.");
            }
        }
    }
    return demo_finish(app, result);
}
