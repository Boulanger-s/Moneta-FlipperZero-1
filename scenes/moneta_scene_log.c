#include "../moneta_i.h"

void moneta_scene_log_on_enter(void* context) {
    MonetaApp* app = context;
    log_view_set_card(app->log_view, &app->card);
    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewLog);
}

bool moneta_scene_log_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void moneta_scene_log_on_exit(void* context) {
    UNUSED(context);
}
