#include "../moneta_i.h"

void moneta_scene_transcript_on_enter(void* context) {
    MonetaApp* app = context;
    transcript_view_set(app->transcript_view, &app->transcript);
    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewTranscript);
}

bool moneta_scene_transcript_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void moneta_scene_transcript_on_exit(void* context) {
    UNUSED(context);
}
