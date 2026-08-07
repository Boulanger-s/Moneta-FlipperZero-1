#include "moneta_i.h"

#define TICK_PERIOD_MS 50

/* ------------------------------------------------------------- feedback */

/* A bad grade should feel different from a good one before you have read a
 * word of it. */
void moneta_notify_result(MonetaApp* app, LeakGrade grade) {
    furi_assert(app);

    if(app->vibro) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
    if(!app->sound) return;

    if(grade >= LeakGradeD) {
        notification_message(app->notifications, &sequence_error);
    } else {
        notification_message(app->notifications, &sequence_success);
    }
}

void moneta_notify_blip(MonetaApp* app) {
    furi_assert(app);
    if(app->sound) notification_message(app->notifications, &sequence_blink_blue_10);
}

/* ------------------------------------------------------------ dispatcher */

static bool moneta_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    MonetaApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool moneta_back_event_callback(void* context) {
    furi_assert(context);
    MonetaApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void moneta_tick_event_callback(void* context) {
    furi_assert(context);
    MonetaApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ------------------------------------------------------------- lifecycle */

static MonetaApp* moneta_app_alloc(void) {
    MonetaApp* app = malloc(sizeof(MonetaApp));
    memset(app, 0, sizeof(MonetaApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&moneta_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, moneta_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, moneta_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, moneta_tick_event_callback, TICK_PERIOD_MS);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MonetaViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, MonetaViewWidget, widget_get_view(app->widget));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        MonetaViewVarItemList,
        variable_item_list_get_view(app->var_item_list));

    app->scan_view = scan_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MonetaViewScan, scan_view_get_view(app->scan_view));

    app->card_view = card_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MonetaViewCard, card_view_get_view(app->card_view));

    app->log_view = log_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MonetaViewLog, log_view_get_view(app->log_view));

    app->lesson_view = lesson_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, MonetaViewLesson, lesson_view_get_view(app->lesson_view));

    app->reader = emv_reader_alloc();

    /* Masked by default. An app that left a full card number sitting on a lit
     * screen would be causing the leak it exists to complain about. */
    app->reveal = MonetaRevealHold;
    app->sound = true;
    app->vibro = true;

    return app;
}

static void moneta_app_free(MonetaApp* app) {
    furi_assert(app);

    emv_reader_free(app->reader);

    view_dispatcher_remove_view(app->view_dispatcher, MonetaViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, MonetaViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, MonetaViewVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, MonetaViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, MonetaViewCard);
    view_dispatcher_remove_view(app->view_dispatcher, MonetaViewLog);
    view_dispatcher_remove_view(app->view_dispatcher, MonetaViewLesson);

    submenu_free(app->submenu);
    widget_free(app->widget);
    variable_item_list_free(app->var_item_list);
    scan_view_free(app->scan_view);
    card_view_free(app->card_view);
    log_view_free(app->log_view);
    lesson_view_free(app->lesson_view);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    /* The card number lived in this struct and nowhere else. Wipe it rather
     * than hand the heap back with a PAN still in it. */
    memset(app, 0, sizeof(MonetaApp));
    free(app);
}

int32_t moneta_app(void* p) {
    UNUSED(p);

    MonetaApp* app = moneta_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, MonetaSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    moneta_app_free(app);
    return 0;
}
