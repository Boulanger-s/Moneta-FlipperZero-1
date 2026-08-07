#include "../moneta_i.h"

/* Every field the card actually gave up, worst first, with what it cost.
 *
 * Only leaked fields are listed. A menu padded with things that did not happen
 * would bury the ones that did.
 */

static void fields_submenu_cb(void* context, uint32_t index) {
    MonetaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void moneta_scene_fields_on_enter(void* context) {
    MonetaApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);

    char header[32];
    snprintf(
        header,
        sizeof(header),
        "Leaked: %u of %u",
        (unsigned)app->report.leaked_count,
        (unsigned)LeakFieldCount);
    submenu_set_header(menu, header);

    for(int f = 0; f < LeakFieldCount; f++) {
        if(!app->report.leaked[f]) continue;
        submenu_add_item(
            menu, leak_field_name((LeakField)f), (uint32_t)f, fields_submenu_cb, app);
    }

    if(app->report.leaked_count == 0) {
        /* Nothing to list is itself the result, and it deserves saying. */
        submenu_add_item(menu, "Nothing was readable", LeakFieldCount, fields_submenu_cb, app);
    }

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, MonetaSceneFields));

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewSubmenu);
}

bool moneta_scene_fields_on_event(void* context, SceneManagerEvent event) {
    MonetaApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= LeakFieldCount) return true; /* the "nothing readable" row */

    scene_manager_set_scene_state(app->scene_manager, MonetaSceneFields, event.event);
    app->explain_field = (LeakField)event.event;
    app->explain_is_safe_fact = false;
    scene_manager_next_scene(app->scene_manager, MonetaSceneExplain);
    return true;
}

void moneta_scene_fields_on_exit(void* context) {
    MonetaApp* app = context;
    submenu_reset(app->submenu);
}
