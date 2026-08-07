#include "card_view.h"

#include <furi.h>
#include <gui/elements.h>

#include "text_fit.h"

/* The result screen, and the whole point of the application.
 *
 * It draws the card back at you: the same rectangle, the same chip, the same
 * number, in the same order it is printed — except this copy came off the air
 * without you touching anything. Reading your own card's last four digits off
 * a Flipper is an argument no paragraph wins.
 *
 * The number stays masked. Revealing it takes a deliberate hold of the OK
 * button and lasts exactly as long as the button is down, because an app that
 * left a full card number sitting on a lit screen would be causing the leak it
 * is complaining about.
 */

#define CARD_X 2
#define CARD_Y 14
#define CARD_W 124
#define CARD_H 33

#define PAN_BASELINE 32
#define PAN_LEFT 8
#define DOT_MAX_SPACING 6
#define DOT_MIN_SPACING 4
#define GROUP_GAP 4
#define LAST4_WIDTH 30

typedef struct {
    EmvCard card;
    LeakReport report;
    bool has_card;
    bool revealed;
    bool reveal_allowed;
    bool expired;
    uint32_t tick;
} CardViewModel;

struct CardView {
    View* view;
    CardViewCallback fields_cb;
    void* fields_ctx;
    CardViewCallback log_cb;
    void* log_ctx;
};

/* The grade, in a filled badge in the corner. Inverted text on a solid block
 * is the loudest thing this screen can say at eleven pixels tall. */
static void draw_grade_badge(Canvas* canvas, LeakGrade grade) {
    const char* text = leak_grade_name(grade);
    canvas_draw_rbox(canvas, 106, 0, 21, 13, 2);
    canvas_invert_color(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 116, 10, AlignCenter, AlignBottom, text);
    canvas_invert_color(canvas);
}

static void draw_chip(Canvas* canvas, int x, int y) {
    canvas_draw_rframe(canvas, x, y, 10, 8, 1);
    canvas_draw_line(canvas, x, y + 4, x + 9, y + 4);
    canvas_draw_line(canvas, x + 4, y, x + 4, y + 7);
}

/* The masked number, drawn as embossed dots rather than asterisks: it reads as
 * a card at a glance, and it makes the four digits that *are* visible the
 * thing your eye lands on. */
static void draw_masked_pan(Canvas* canvas, const EmvCard* card) {
    size_t pan_len = card->pan_len;
    if(pan_len < 5) return;

    size_t hidden = pan_len - 4;

    /* Fit the dots into whatever is left after the last four digits. A
     * 19-digit Maestro number needs tighter spacing than a 16-digit Visa.
     *
     * The group gaps have to come out of the budget too: at 19 digits there
     * are three of them, and leaving them out ran the last four digits past
     * the edge of the card. */
    int groups = (int)((hidden - 1) / 4); /* gaps between groups of four */
    int avail = CARD_W - (PAN_LEFT - CARD_X) - LAST4_WIDTH - GROUP_GAP * (groups + 1) - 4;
    int spacing = avail / (int)hidden;
    if(spacing > DOT_MAX_SPACING) spacing = DOT_MAX_SPACING;
    if(spacing < DOT_MIN_SPACING) spacing = DOT_MIN_SPACING;

    int x = PAN_LEFT;
    for(size_t i = 0; i < hidden; i++) {
        if(i > 0 && (i % 4) == 0) x += GROUP_GAP;
        canvas_draw_disc(canvas, x + 1, PAN_BASELINE - 3, 1);
        x += spacing;
    }

    x += GROUP_GAP;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, x, PAN_BASELINE, card->pan + hidden);
}

static void draw_revealed_pan(Canvas* canvas, const EmvCard* card) {
    char pan[MONETA_PAN_DIGITS_MAX + 8];
    emv_card_format_pan(card, pan, sizeof(pan));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, PAN_LEFT - 2, PAN_BASELINE, pan);
}

/* How much of the card left the card, as a bar. A number on its own invites
 * arguing about the number; a bar that is nearly full does not. */
static void draw_exposure_bar(Canvas* canvas, uint8_t score) {
    /* The bar stops at x=84 so the percentage beside it has clear air, and it
     * sits a row higher than it used to so the hint line below never rides up
     * against it. Both numbers came out of the mock-up renderer, which draws
     * from these same constants. */
    const int x = 4, y = 48, w = 76, h = 7;
    canvas_draw_rframe(canvas, x, y, w, h, 1);

    int fill = (int)score * (w - 4) / 100;
    if(fill > 0) canvas_draw_box(canvas, x + 2, y + 2, fill, h - 4);

    char text[16];
    snprintf(text, sizeof(text), "%u%% out", (unsigned)score);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, y + 6, AlignRight, AlignBottom, text);
}

static void draw_empty(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignBottom, "No card read");
}

static void card_view_draw(Canvas* canvas, void* model) {
    CardViewModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(!m->has_card) {
        draw_empty(canvas);
        return;
    }

    const EmvCard* c = &m->card;

    /* --- header: what the card calls itself, and its grade --- */
    canvas_set_font(canvas, FontPrimary);
    const char* title = c->has_label ? c->label : emv_scheme_name(c->scheme);
    char header[20];
    strncpy(header, title, sizeof(header) - 1);
    header[sizeof(header) - 1] = '\0';
    text_fit_width(canvas, header, 100);
    canvas_draw_str(canvas, 2, 10, header);

    draw_grade_badge(canvas, m->report.grade);

    /* --- the card itself --- */
    canvas_draw_rframe(canvas, CARD_X, CARD_Y, CARD_W, CARD_H, 3);

    if(c->has_pan) {
        if(m->revealed && m->reveal_allowed) {
            draw_revealed_pan(canvas, c);
        } else {
            draw_masked_pan(canvas, c);
        }
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, PAN_LEFT, PAN_BASELINE, "number not readable");
        draw_chip(canvas, 108, 19);
    }

    canvas_set_font(canvas, FontSecondary);

    char exp[16];
    emv_card_format_expiry(c, exp, sizeof(exp));
    char expline[24];
    snprintf(expline, sizeof(expline), "EXP %s", exp);
    canvas_draw_str(canvas, PAN_LEFT - 2, 43, expline);

    /* The right-hand end of the expiry row holds exactly one thing.
     *
     * An expired card still answers every question it ever did — the chip does
     * not stop talking on the date printed on the front — so that warning wins
     * the space when it applies. The cardholder name is not lost by giving way
     * here: it is listed, weighted and explained under "What leaked". */
    if(m->expired) {
        canvas_draw_str_aligned(canvas, 122, 43, AlignRight, AlignBottom, "EXPIRED");
    } else if(c->has_name) {
        char name[MONETA_NAME_MAX + 1];
        strncpy(name, c->name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        text_fit_width(canvas, name, 68);
        canvas_draw_str_aligned(canvas, 122, 43, AlignRight, AlignBottom, name);
    } else {
        canvas_draw_str_aligned(
            canvas, 122, 43, AlignRight, AlignBottom, emv_scheme_name(c->scheme));
    }

    /* --- verdict --- */
    draw_exposure_bar(canvas, m->report.score);

    /* One hint at a time, alternating, because three of them will not fit on a
     * 128-pixel line and a truncated hint helps nobody. */
    const char* hint;
    if(m->revealed && m->reveal_allowed) {
        hint = "Release to hide";
    } else {
        bool phase = ((m->tick / 20) % 2) == 1;
        if(phase && c->log_num > 0) {
            hint = "Right: spending log";
        } else if(phase && m->reveal_allowed && c->has_pan) {
            hint = "Hold OK: show number";
        } else {
            hint = "OK: what leaked";
        }
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, hint);
}

static bool card_view_input(InputEvent* event, void* context) {
    CardView* view = context;
    bool consumed = false;

    if(event->key == InputKeyOk) {
        if(event->type == InputTypeShort) {
            if(view->fields_cb) view->fields_cb(view->fields_ctx);
            consumed = true;
        } else if(event->type == InputTypeLong) {
            with_view_model(
                view->view, CardViewModel * m, { m->revealed = true; }, true);
            consumed = true;
        } else if(event->type == InputTypeRelease) {
            /* The reveal lasts exactly as long as the button is held. */
            with_view_model(
                view->view, CardViewModel * m, { m->revealed = false; }, true);
            consumed = true;
        }
    } else if(event->key == InputKeyRight && event->type == InputTypeShort) {
        bool has_log = false;
        with_view_model(
            view->view, CardViewModel * m, { has_log = m->card.log_num > 0; }, false);
        if(has_log && view->log_cb) {
            view->log_cb(view->log_ctx);
            consumed = true;
        }
    }

    return consumed;
}

CardView* card_view_alloc(void) {
    CardView* view = malloc(sizeof(CardView));
    memset(view, 0, sizeof(CardView));
    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(CardViewModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, card_view_draw);
    view_set_input_callback(view->view, card_view_input);

    with_view_model(
        view->view, CardViewModel * m, { m->reveal_allowed = true; }, false);
    return view;
}

void card_view_free(CardView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* card_view_get_view(CardView* view) {
    furi_assert(view);
    return view->view;
}

void card_view_set_card(CardView* view, const EmvCard* card, const LeakReport* report) {
    furi_assert(view);
    with_view_model(
        view->view,
        CardViewModel * m,
        {
            m->card = *card;
            m->report = *report;
            m->has_card = true;
            m->revealed = false;
            m->tick = 0;
        },
        true);
}

void card_view_set_reveal_allowed(CardView* view, bool allowed) {
    furi_assert(view);
    with_view_model(
        view->view,
        CardViewModel * m,
        {
            m->reveal_allowed = allowed;
            if(!allowed) m->revealed = false;
        },
        true);
}

void card_view_set_expired(CardView* view, bool expired) {
    furi_assert(view);
    with_view_model(
        view->view, CardViewModel * m, { m->expired = expired; }, true);
}

void card_view_tick(CardView* view) {
    furi_assert(view);
    with_view_model(
        view->view, CardViewModel * m, { m->tick++; }, true);
}

void card_view_set_fields_callback(CardView* view, CardViewCallback cb, void* context) {
    furi_assert(view);
    view->fields_cb = cb;
    view->fields_ctx = context;
}

void card_view_set_log_callback(CardView* view, CardViewCallback cb, void* context) {
    furi_assert(view);
    view->log_cb = cb;
    view->log_ctx = context;
}
