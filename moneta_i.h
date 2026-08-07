#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "moneta_icons.h" // generated from icons/ by fbt

#include "helpers/demo_card.h"
#include "helpers/emv_card.h"
#include "helpers/emv_reader.h"
#include "helpers/leak_grade.h"
#include "scenes/moneta_scene.h"
#include "views/card_view.h"
#include "views/lesson_view.h"
#include "views/log_view.h"
#include "views/scan_view.h"

#define MONETA_VERSION "1.0"

typedef enum {
    MonetaViewSubmenu,
    MonetaViewWidget,
    MonetaViewVarItemList,
    MonetaViewScan,
    MonetaViewCard,
    MonetaViewLog,
    MonetaViewLesson,
} MonetaViewId;

typedef enum {
    MonetaCustomEventCardRead = 100, /* the reader finished a card */
    MonetaCustomEventNotPayment, /* something in the field, but not a card */
    MonetaCustomEventShowFields, /* user opened the field list */
    MonetaCustomEventShowLog, /* user opened the spending log */
    MonetaCustomEventRescan, /* user asked for another card */
} MonetaCustomEvent;

/* Whether the full card number may ever be put on screen. Masked is the
 * default: the point is made by the last four digits, and a number sitting
 * unattended on a bright screen is a leak this app would itself be causing. */
typedef enum {
    MonetaRevealHold = 0, /* hold OK to reveal, released when you let go */
    MonetaRevealNever,
} MonetaRevealMode;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    Widget* widget;
    VariableItemList* var_item_list;
    ScanView* scan_view;
    CardView* card_view;
    LogView* log_view;
    LessonView* lesson_view;

    EmvReader* reader;

    /* settings */
    MonetaRevealMode reveal;
    bool sound;
    bool vibro;

    /* the card currently on screen */
    EmvCard card;
    LeakReport report;
    bool have_card;

    /* which field the explain scene is about */
    LeakField explain_field;
    bool explain_is_safe_fact;
    LeakSafeFact explain_safe;
} MonetaApp;

/* Feedback, gated by settings (defined in moneta.c). */
void moneta_notify_result(MonetaApp* app, LeakGrade grade);
void moneta_notify_blip(MonetaApp* app);
