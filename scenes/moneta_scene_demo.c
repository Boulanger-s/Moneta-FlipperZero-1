#include "../moneta_i.h"

/* Three saved cards for when there is no card to hand — a talk, a classroom,
 * a screenshot. They are parsed, not faked: the same code path, the same
 * grader, the same result screen. */

static void demo_submenu_cb(void* context, uint32_t index) {
    MonetaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void moneta_scene_demo_on_enter(void* context) {
    MonetaApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);
    submenu_set_header(menu, "Demo cards");

    for(size_t i = 0; i < demo_card_count(); i++) {
        submenu_add_item(menu, demo_card_title(i), (uint32_t)i, demo_submenu_cb, app);
    }

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, MonetaSceneDemo));

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewSubmenu);
}

bool moneta_scene_demo_on_event(void* context, SceneManagerEvent event) {
    MonetaApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= demo_card_count()) return true;

    scene_manager_set_scene_state(app->scene_manager, MonetaSceneDemo, event.event);

    if(demo_card_load(event.event, &app->card)) {
        leak_grade(&app->card, &app->report);
        memset(&app->transcript, 0, sizeof(app->transcript));
        app->have_card = true;
        moneta_notify_result(app, app->report.grade);
        scene_manager_next_scene(app->scene_manager, MonetaSceneResult);
    }
    return true;
}

void moneta_scene_demo_on_exit(void* context) {
    MonetaApp* app = context;
    submenu_reset(app->submenu);
}
