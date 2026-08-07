#include "../moneta_i.h"

static void result_report_cb(void* context) {
    MonetaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, MonetaCustomEventShowReport);
}

static void result_log_cb(void* context) {
    MonetaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, MonetaCustomEventShowLog);
}

void moneta_scene_result_on_enter(void* context) {
    MonetaApp* app = context;

    card_view_set_card(app->card_view, &app->card, &app->report);
    card_view_set_reveal_allowed(app->card_view, app->reveal == MonetaRevealHold);
    card_view_set_fields_callback(app->card_view, result_report_cb, app);
    card_view_set_expired(app->card_view, moneta_card_expired(app));
    card_view_set_log_callback(app->card_view, result_log_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewCard);
}

bool moneta_scene_result_on_event(void* context, SceneManagerEvent event) {
    MonetaApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        card_view_tick(app->card_view);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case MonetaCustomEventShowReport:
            scene_manager_next_scene(app->scene_manager, MonetaSceneReport);
            return true;
        case MonetaCustomEventShowLog:
            scene_manager_next_scene(app->scene_manager, MonetaSceneLog);
            return true;
        default:
            return false;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        /* Back from a result goes home, not back into the scan we came
         * through — nobody wants to re-read a card by pressing Back once. */
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, MonetaSceneStart);
        return true;
    }

    return false;
}

void moneta_scene_result_on_exit(void* context) {
    UNUSED(context);
}
