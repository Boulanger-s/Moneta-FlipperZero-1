#include "../moneta_i.h"

/* The hub reached from the result screen.
 *
 * Everything about the card in front of you hangs off here: what leaked, the
 * technical detail behind it, the spending log, the raw conversation, and the
 * option to keep a copy. Rows that have nothing behind them are not shown —
 * offering a spending log for a card that keeps none teaches the wrong thing.
 */

typedef enum {
    ReportItemFields,
    ReportItemDetails,
    ReportItemLog,
    ReportItemTranscript,
    ReportItemSave,
} ReportItem;

static void report_submenu_cb(void* context, uint32_t index) {
    MonetaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void moneta_scene_report_on_enter(void* context) {
    MonetaApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);

    char header[32];
    snprintf(
        header,
        sizeof(header),
        "Grade %s - %u%% out",
        leak_grade_name(app->report.grade),
        (unsigned)app->report.score);
    submenu_set_header(menu, header);

    submenu_add_item(menu, "What leaked", ReportItemFields, report_submenu_cb, app);
    submenu_add_item(menu, "Card details", ReportItemDetails, report_submenu_cb, app);
    if(app->card.log_num > 0) {
        submenu_add_item(menu, "Spending log", ReportItemLog, report_submenu_cb, app);
    }
    if(app->transcript.num > 0) {
        submenu_add_item(menu, "Command transcript", ReportItemTranscript, report_submenu_cb, app);
    }
    submenu_add_item(menu, "Save report to SD", ReportItemSave, report_submenu_cb, app);

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, MonetaSceneReport));

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewSubmenu);
}

bool moneta_scene_report_on_event(void* context, SceneManagerEvent event) {
    MonetaApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, MonetaSceneReport, event.event);

    switch(event.event) {
    case ReportItemFields:
        scene_manager_next_scene(app->scene_manager, MonetaSceneFields);
        return true;
    case ReportItemDetails:
        scene_manager_next_scene(app->scene_manager, MonetaSceneDetails);
        return true;
    case ReportItemLog:
        scene_manager_next_scene(app->scene_manager, MonetaSceneLog);
        return true;
    case ReportItemTranscript:
        scene_manager_next_scene(app->scene_manager, MonetaSceneTranscript);
        return true;
    case ReportItemSave:
        scene_manager_next_scene(app->scene_manager, MonetaSceneSave);
        return true;
    default:
        return false;
    }
}

void moneta_scene_report_on_exit(void* context) {
    MonetaApp* app = context;
    submenu_reset(app->submenu);
}
