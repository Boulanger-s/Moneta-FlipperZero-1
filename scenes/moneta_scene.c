#include "moneta_scene.h"

// Generate the scene handler tables
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const moneta_scene_on_enter_handlers[])(void*) = {
#include "moneta_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const moneta_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "moneta_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const moneta_scene_on_exit_handlers[])(void* context) = {
#include "moneta_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers moneta_scene_handlers = {
    .on_enter_handlers = moneta_scene_on_enter_handlers,
    .on_event_handlers = moneta_scene_on_event_handlers,
    .on_exit_handlers = moneta_scene_on_exit_handlers,
    .scene_num = MonetaSceneNum,
};
