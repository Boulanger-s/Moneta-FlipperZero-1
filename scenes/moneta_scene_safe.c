#include "../moneta_i.h"

/* The other half of an honest answer.
 *
 * Reachable from the main menu without reading a card at all, because the
 * people most likely to be frightened by this app are the ones least likely to
 * own a Flipper — and "they cannot get your PIN" travels further than any
 * hex dump.
 */

static void safe_submenu_cb(void* context, uint32_t index) {
    MonetaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void moneta_scene_safe_on_enter(void* context) {
    MonetaApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);
    submenu_set_header(menu, "Safe from any reader");

    for(int f = 0; f < LeakSafeCount; f++) {
        submenu_add_item(
            menu, leak_safe_name((LeakSafeFact)f), (uint32_t)f, safe_submenu_cb, app);
    }

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, MonetaSceneSafe));

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewSubmenu);
}

bool moneta_scene_safe_on_event(void* context, SceneManagerEvent event) {
    MonetaApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= LeakSafeCount) return true;

    scene_manager_set_scene_state(app->scene_manager, MonetaSceneSafe, event.event);
    app->explain_safe = (LeakSafeFact)event.event;
    app->explain_is_safe_fact = true;
    scene_manager_next_scene(app->scene_manager, MonetaSceneExplain);
    return true;
}

void moneta_scene_safe_on_exit(void* context) {
    MonetaApp* app = context;
    submenu_reset(app->submenu);
}
