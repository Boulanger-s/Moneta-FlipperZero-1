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
#include "helpers/report.h"
#include "scenes/moneta_scene.h"
#include "views/card_view.h"
#include "views/lesson_view.h"
#include "views/log_view.h"
#include "views/scan_view.h"
#include "views/transcript_view.h"

#define MONETA_VERSION "1.1"

typedef enum {
    MonetaViewSubmenu,
    MonetaViewWidget,
    MonetaViewVarItemList,
    MonetaViewScan,
    MonetaViewCard,
    MonetaViewLog,
    MonetaViewLesson,
    MonetaViewTranscript,
} MonetaViewId;

typedef enum {
    MonetaCustomEventCardRead = 100, /* the reader finished a card */
    MonetaCustomEventShowReport, /* user opened the report hub */
    MonetaCustomEventShowLog, /* user jumped straight to the spending log */
} MonetaCustomEvent;

/* Whether the full card number may ever be put on screen. Masked is the
 * default: the point is made by the last four digits, and a number sitting
 * unattended on a bright screen is a leak this app would itself be causing. */
typedef enum {
    MonetaRevealHold = 0, /* hold OK to reveal, released when you let go */
    MonetaRevealNever,
} MonetaRevealMode;

/* What this Flipper has seen since the app was opened. Kept in RAM only, so
 * closing Moneta forgets every card it read. Useful at a talk: "nine cards in
 * this room, seven of them graded D or worse." */
typedef struct {
    uint8_t cards; /* real cards, demos excluded */
    uint8_t by_grade[LeakGradeF + 1];
    uint8_t leaked_name;
    uint8_t leaked_log;
    uint8_t spendable; /* number + expiry both readable */
    uint16_t score_total;
} MonetaSession;

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
    TranscriptView* transcript_view;

    EmvReader* reader;

    /* settings */
    MonetaRevealMode reveal;
    bool sound;
    bool vibro;

    /* the card currently on screen */
    EmvCard card;
    LeakReport report;
    EmvTranscript transcript;
    bool have_card;

    MonetaSession session;

    /* which field the explain scene is about */
    LeakField explain_field;
    bool explain_is_safe_fact;
    LeakSafeFact explain_safe;
} MonetaApp;

/* Feedback, gated by settings (defined in moneta.c). */
void moneta_notify_result(MonetaApp* app, LeakGrade grade);
/* Fold the card currently on screen into the session tally. Demo cards are
 * ignored, because a tally that counted them would not be a fact about the
 * room. */
void moneta_session_add(MonetaApp* app);
/* True when the card on screen has an expiry date already in the past. */
bool moneta_card_expired(const MonetaApp* app);
