# Process dialog reference investigation

The requested `FormatDialogs/DialogProcess.cpp` is named
`_mylibs/FormatDialogs/xdialogprocess.cpp` in this checkout. The implementation,
header, `.ui` form, and XBinary progress helpers were examined before adding
`xxwidgets_process_dialog`.

The Qt dialog owns five progress bars, polls every 100 ms, and shows a bar while
its record's `bIsValid` is true. Its Advanced checkbox gates levels 1–4 and
defaults to checked. The xxwidgets form shows every busy level, as requested,
with no additional visibility gate. Status and current/total are displayed
above each bar so native backends that cannot overlay progress text still show
the same information. Percentages clamp to 0–100 and avoid integer overflow.
Records with no known total show zero percent, matching the reference.

`showDialogDelay` defaults to 1,000 ms. It sleeps on the GUI thread while checking
worker completion, then tests `isSuccess()` to decide whether to call `exec()`.
`isSuccess()` changes only in the queued `onCompleted` slot. That slot cannot
run during the sleep loop, so a fast completed job can still enter the dialog
briefly. The new dialog pumps events during the delay and checks explicit
completion before creating any window, including on the update that crosses
the threshold. Its clock is monotonic; exactly 1,000 ms remains hidden.

The reference's Cancel button requests a stop; completion accepts/rejects the
dialog, and destruction waits for its QThread. The new form also requests stop
and waits for explicit completion on Cancel/Escape/close. It does not own or
create a worker thread. Callback/UI errors and app shutdown deliver a final
stop request; the caller owns worker shutdown and joining.

XBinary's current `PDSTRUCT` has atomic scalar fields, a mutex-protected snapshot
API, and an `nFinished` count. Its finished helper checks both absence of valid
records and evidence of at least one completed record. xxfclib's `xx_pd_struct`
instead has ordinary 64-bit counters, fixed status buffers, a volatile stop
flag, and no completion field or mutex. Treating it as a concurrently readable
worker object or treating five idle records as completion would be unsafe.
The new UI callback therefore supplies a coherent snapshot and an independent
completion result. It can also advance bounded work directly on the UI thread.

The Qt speed labels increment a sample count on every timer update and display
current/sample-count, rather than elapsed-time throughput. Remaining time is
estimated from record 0. These estimates and the Advanced checkbox were not
ported: this request specifies busy bars and delayed visibility. Elapsed time
and cancellation are included; xxfclib error fields remain available in the
final snapshot without introducing another modal message box.
