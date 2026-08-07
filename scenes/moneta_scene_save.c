#include "../moneta_i.h"

#include <storage/storage.h>

/* Write the exposure report to the SD card.
 *
 * The text comes from report_build, which is Flipper-free precisely so a host
 * test can prove the full card number never appears in it. This scene only
 * finds a free filename and writes the bytes; it does not compose them, and it
 * must not start.
 */

#define MONETA_REPORT_DIR EXT_PATH("apps_data/moneta")
#define MONETA_REPORT_MAX_FILES 999

static bool save_pick_path(Storage* storage, char* path, size_t path_len) {
    FileInfo info;
    for(unsigned i = 1; i <= MONETA_REPORT_MAX_FILES; i++) {
        snprintf(path, path_len, "%s/report_%03u.txt", MONETA_REPORT_DIR, i);
        if(storage_common_stat(storage, path, &info) != FSE_OK) return true;
    }
    return false;
}

/* Returns the path written, or NULL with `error` describing why not. */
static bool save_report(MonetaApp* app, char* path, size_t path_len, const char** error) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = false;

    do {
        if(!storage_simply_mkdir(storage, MONETA_REPORT_DIR)) {
            /* mkdir also returns false when the directory is already there,
             * so this is only fatal if we then cannot write into it. */
        }

        if(!save_pick_path(storage, path, path_len)) {
            *error = "No free filename left.";
            break;
        }

        char* text = malloc(MONETA_REPORT_MAX);
        DateTime now;
        furi_hal_rtc_get_datetime(&now);
        char stamp[32];
        snprintf(
            stamp,
            sizeof(stamp),
            "%04u-%02u-%02u %02u:%02u",
            (unsigned)now.year,
            (unsigned)now.month,
            (unsigned)now.day,
            (unsigned)now.hour,
            (unsigned)now.minute);

        size_t len =
            report_build(&app->card, &app->report, MONETA_VERSION, stamp, text, MONETA_REPORT_MAX);

        File* file = storage_file_alloc(storage);
        if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            ok = storage_file_write(file, text, len) == len;
            if(!ok) *error = "Write failed. Card full?";
            storage_file_close(file);
        } else {
            *error = "Could not create the file.";
        }
        storage_file_free(file);

        /* The report holds the last four digits and a grade, but wipe the
         * buffer anyway rather than leave card data in freed heap. */
        memset(text, 0, MONETA_REPORT_MAX);
        free(text);
    } while(0);

    furi_record_close(RECORD_STORAGE);
    return ok;
}

void moneta_scene_save_on_enter(void* context) {
    MonetaApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    char path[128] = {0};
    const char* error = "Unknown error.";
    bool ok = save_report(app, path, sizeof(path), &error);

    FuriString* text = furi_string_alloc();
    if(ok) {
        /* Show the path relative to the SD root — the full EXT_PATH prefix is
         * noise on a 128-pixel screen. */
        const char* shown = strstr(path, "apps_data");
        furi_string_printf(
            text,
            "\e#Report saved\e#\n\n%s\n\n"
            "It records the grade, every field that leaked, and the last four "
            "digits.\n\n"
            "It does not contain the full card number. Nothing Moneta writes "
            "to the SD card ever does.\n",
            shown ? shown : path);
        notification_message(app->notifications, &sequence_success);
    } else {
        furi_string_printf(text, "\e#Not saved\e#\n\n%s\n", error);
        notification_message(app->notifications, &sequence_error);
    }

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewWidget);
}

bool moneta_scene_save_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void moneta_scene_save_on_exit(void* context) {
    MonetaApp* app = context;
    widget_reset(app->widget);
}
