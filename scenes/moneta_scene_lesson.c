#include "../moneta_i.h"

void moneta_scene_lesson_on_enter(void* context) {
    MonetaApp* app = context;
    lesson_view_reset(app->lesson_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewLesson);
}

bool moneta_scene_lesson_on_event(void* context, SceneManagerEvent event) {
    MonetaApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        lesson_view_tick(app->lesson_view);
        return true;
    }
    return false;
}

void moneta_scene_lesson_on_exit(void* context) {
    UNUSED(context);
}
