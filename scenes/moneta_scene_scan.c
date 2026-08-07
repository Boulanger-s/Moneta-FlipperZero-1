#include "../moneta_i.h"

/* Holds the field up until a card is read or the user leaves.
 *
 * The reader lives on its own thread and is polled from the scene tick rather
 * than pushing into the GUI, which keeps every canvas touch on the GUI thread
 * and makes leaving the scene a simple matter of joining one worker.
 */

void moneta_scene_scan_on_enter(void* context) {
    MonetaApp* app = context;

    app->have_card = false;
    emv_card_reset(&app->card);

    EmvReaderProgress progress = {0};
    progress.state = EmvReaderSearching;
    scan_view_set_progress(app->scan_view, &progress);

    emv_reader_start(app->reader);
    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewScan);
}

bool moneta_scene_scan_on_event(void* context, SceneManagerEvent event) {
    MonetaApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        scan_view_tick(app->scan_view);

        EmvReaderProgress progress;
        emv_reader_progress(app->reader, &progress);
        scan_view_set_progress(app->scan_view, &progress);

        if(progress.state == EmvReaderDone && !app->have_card) {
            if(emv_reader_get_card(app->reader, &app->card)) {
                leak_grade(&app->card, &app->report);
                app->have_card = true;
                view_dispatcher_send_custom_event(
                    app->view_dispatcher, MonetaCustomEventCardRead);
            }
        }
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom && event.event == MonetaCustomEventCardRead) {
        moneta_notify_result(app, app->report.grade);
        scene_manager_next_scene(app->scene_manager, MonetaSceneResult);
        return true;
    }

    return false;
}

void moneta_scene_scan_on_exit(void* context) {
    MonetaApp* app = context;
    /* Joins the worker and drops the field. Nothing keeps the radio alive
     * behind a screen the user has walked away from. */
    emv_reader_stop(app->reader);
}
