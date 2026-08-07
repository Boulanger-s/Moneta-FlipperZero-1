#include "../moneta_i.h"

typedef enum {
    StartItemRead,
    StartItemLesson,
    StartItemSafe,
    StartItemDemo,
    StartItemSettings,
    StartItemAbout,
} StartItem;

static void start_submenu_cb(void* context, uint32_t index) {
    MonetaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void moneta_scene_start_on_enter(void* context) {
    MonetaApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);
    submenu_set_header(menu, "Moneta");
    submenu_add_item(menu, "Read my card", StartItemRead, start_submenu_cb, app);
    submenu_add_item(menu, "What a skimmer gets", StartItemLesson, start_submenu_cb, app);
    submenu_add_item(menu, "What it cannot get", StartItemSafe, start_submenu_cb, app);
    submenu_add_item(menu, "Demo cards", StartItemDemo, start_submenu_cb, app);
    submenu_add_item(menu, "Settings", StartItemSettings, start_submenu_cb, app);
    submenu_add_item(menu, "About", StartItemAbout, start_submenu_cb, app);

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, MonetaSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewSubmenu);
}

bool moneta_scene_start_on_event(void* context, SceneManagerEvent event) {
    MonetaApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, MonetaSceneStart, event.event);

    switch(event.event) {
    case StartItemRead:
        scene_manager_next_scene(app->scene_manager, MonetaSceneScan);
        return true;
    case StartItemLesson:
        scene_manager_next_scene(app->scene_manager, MonetaSceneLesson);
        return true;
    case StartItemSafe:
        scene_manager_next_scene(app->scene_manager, MonetaSceneSafe);
        return true;
    case StartItemDemo:
        scene_manager_next_scene(app->scene_manager, MonetaSceneDemo);
        return true;
    case StartItemSettings:
        scene_manager_next_scene(app->scene_manager, MonetaSceneSettings);
        return true;
    case StartItemAbout:
        scene_manager_next_scene(app->scene_manager, MonetaSceneAbout);
        return true;
    default:
        return false;
    }
}

void moneta_scene_start_on_exit(void* context) {
    MonetaApp* app = context;
    submenu_reset(app->submenu);
}
