#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_process.h>
#include <string.h>

int main(void)
{
    xx_meta_string record = {0};
    xx_var value = {0};
    xx_pd_struct progress = {0};
    record.var.type = XX_VAR_TYPE_UINT64;
    return strcmp(xxwidgets_status_string(XXWIDGETS_OK), "success") != 0 ||
        xxwidgets_combobox_count(NULL) != 0 ||
        xxwidgets_combobox_get_current(NULL, &value) != XXWIDGETS_INVALID_ARGUMENT ||
        record.var.type != XX_VAR_TYPE_UINT64 ||
        xxwidgets_process_dialog(NULL, "Process", &progress, NULL, NULL) != XXWIDGETS_INVALID_ARGUMENT;
}
