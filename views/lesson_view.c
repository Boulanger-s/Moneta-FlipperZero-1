#include "lesson_view.h"

#include <furi.h>
#include <gui/elements.h>

/* Five animated panels explaining what a contactless skim actually is.
 *
 * The last panel is the one that matters most, and it is the one a scare piece
 * would leave out: this attack does not let anyone tap your card at a shop. It
 * lets them type your number into a website. Getting that distinction right is
 * the difference between teaching someone and frightening them.
 */

#define LESSON_FRAMES 5

typedef struct {
    uint8_t frame;
    uint32_t tick;
} LessonViewModel;

struct LessonView {
    View* view;
};

/* ------------------------------------------------------------- primitives */

static void draw_person(Canvas* canvas, int x, int y) {
    canvas_draw_circle(canvas, x, y, 3);
    canvas_draw_line(canvas, x, y + 4, x, y + 13); /* body */
    canvas_draw_line(canvas, x, y + 6, x - 4, y + 10); /* arms */
    canvas_draw_line(canvas, x, y + 6, x + 4, y + 10);
    canvas_draw_line(canvas, x, y + 13, x - 3, y + 19); /* legs */
    canvas_draw_line(canvas, x, y + 13, x + 3, y + 19);
}

static void draw_small_card(Canvas* canvas, int x, int y) {
    canvas_draw_rframe(canvas, x, y, 14, 10, 1);
    canvas_draw_box(canvas, x + 2, y + 3, 4, 3); /* the chip */
}

static void draw_bag(Canvas* canvas, int x, int y) {
    canvas_draw_rframe(canvas, x, y, 20, 16, 2);
    canvas_draw_line(canvas, x + 5, y, x + 7, y - 4); /* handle */
    canvas_draw_line(canvas, x + 7, y - 4, x + 13, y - 4);
    canvas_draw_line(canvas, x + 13, y - 4, x + 15, y);
    canvas_draw_rframe(canvas, x + 4, y + 5, 12, 8, 1); /* the reader inside */
}

/* An expanding arc, as a run of dots on a circle. There is no arc primitive in
 * the firmware API, so this is the honest way to get one. */
static void draw_arc_dots(Canvas* canvas, int cx, int cy, int radius, int spread) {
    for(int i = -spread; i <= spread; i++) {
        int dy = i * radius / (spread + 2);
        int dx2 = radius * radius - dy * dy;
        if(dx2 <= 0) continue;
        int dx = 0;
        while((dx + 1) * (dx + 1) <= dx2) dx++;
        canvas_draw_dot(canvas, cx + dx, cy + dy);
    }
}

/* --------------------------------------------------------------- panels */

/* 1. The card is always listening. */
static void panel_always_on(Canvas* canvas, uint32_t tick) {
    draw_person(canvas, 26, 16);
    draw_small_card(canvas, 30, 26);

    int radius = 6 + (int)(tick % 10);
    draw_arc_dots(canvas, 45, 31, radius, 4);
    if(tick % 10 > 4) draw_arc_dots(canvas, 45, 31, radius - 5, 3);

    canvas_draw_str(canvas, 76, 24, "no PIN");
    canvas_draw_str(canvas, 76, 34, "no tap");
    canvas_draw_str(canvas, 76, 44, "no trace");
}

/* 2. Range is the whole defence, and it is small. */
static void panel_range(Canvas* canvas, uint32_t tick) {
    draw_person(canvas, 14, 16);
    draw_small_card(canvas, 18, 26);
    draw_bag(canvas, 84, 22);

    /* The bag creeps closer, then resets. */
    int reach = 30 + (int)(tick % 16);
    draw_arc_dots(canvas, 34, 31, reach - 24, 4);

    canvas_draw_line(canvas, 36, 44, 82, 44);
    canvas_draw_line(canvas, 36, 42, 36, 46);
    canvas_draw_line(canvas, 82, 42, 82, 46);
    canvas_draw_str(canvas, 46, 42, "2-5 cm");
}

/* 3. The conversation itself. */
static void panel_commands(Canvas* canvas, uint32_t tick) {
    canvas_draw_rframe(canvas, 2, 16, 40, 26, 2);
    canvas_draw_rframe(canvas, 86, 16, 40, 26, 2);

    canvas_draw_str(canvas, 8, 27, "READER");
    canvas_draw_str(canvas, 96, 27, "CARD");

    /* A packet crossing the gap, out and back. */
    uint32_t phase = tick % 24;
    bool outward = phase < 12;
    int t = (int)(outward ? phase : 23 - phase);
    int x = 46 + t * 3;

    canvas_draw_line(canvas, 44, 24, 84, 24);
    canvas_draw_line(canvas, 44, 36, 84, 36);
    canvas_draw_box(canvas, x, outward ? 22 : 34, 4, 4);

    canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignBottom, "8 commands, 1 second");
}

/* 4. What comes back. */
static void panel_haul(Canvas* canvas, uint32_t tick) {
    draw_small_card(canvas, 4, 26);

    /* Fields drifting away from the card. */
    int drift = (int)(tick % 12);
    canvas_draw_str(canvas, 26 + drift / 3, 22, "card number");
    canvas_draw_str(canvas, 26 + drift / 3, 32, "expiry date");
    canvas_draw_str(canvas, 26 + drift / 3, 42, "sometimes your name");

    canvas_draw_line(canvas, 20, 31, 24 + drift / 3, 31);
}

/* 5. The limit. This panel is the reason the app is honest rather than
 *    alarming: the leak is real, and it is not the leak people assume. */
static void panel_limits(Canvas* canvas, uint32_t tick) {
    /* Two boxes, each a symbol over a caption. The symbol occupies y 19..33
     * and the caption sits on baseline 42, so nothing is drawn through a word
     * — the earlier version put the cross straight across its own label. */

    /* Left: a checkout form, which this leak does enable. */
    canvas_draw_rframe(canvas, 4, 16, 52, 30, 2);
    if((tick / 6) % 2) {
        canvas_draw_line(canvas, 16, 27, 22, 33);
        canvas_draw_line(canvas, 22, 33, 40, 19);
    }
    canvas_draw_str_aligned(canvas, 30, 42, AlignCenter, AlignBottom, "checkout");

    /* Right: a shop terminal, which it does not. */
    canvas_draw_rframe(canvas, 72, 16, 52, 30, 2);
    canvas_draw_line(canvas, 86, 20, 110, 33);
    canvas_draw_line(canvas, 110, 20, 86, 33);
    canvas_draw_str_aligned(canvas, 98, 42, AlignCenter, AlignBottom, "shop tap");
}

/* --------------------------------------------------------------- content */

typedef struct {
    const char* title;
    const char* line1;
    const char* line2;
    void (*draw)(Canvas*, uint32_t);
} LessonFrame;

/* Caption lines are kept to about 25 characters. FontSecondary averages five
 * pixels a glyph, so anything longer runs off a 128-pixel screen — which is
 * what tools_gen_mockups.py exists to catch before a build ships. */
static const LessonFrame FRAMES[LESSON_FRAMES] = {
    {
        "ALWAYS ANSWERING",
        "Your card answers any",
        "reader that asks it.",
        panel_always_on,
    },
    {
        "IT NEEDS TO BE CLOSE",
        "A hidden reader must get",
        "within a few centimetres.",
        panel_range,
    },
    {
        "THE CONVERSATION",
        "The same commands a shop",
        "sends, minus the payment.",
        panel_commands,
    },
    {
        "WHAT COMES BACK",
        "Number and expiry date.",
        "Never the CVV or the PIN.",
        panel_haul,
    },
    {
        "WHAT IT IS WORTH",
        "Enough to type into a",
        "checkout. Not to tap.",
        panel_limits,
    },
};

static void lesson_view_draw(Canvas* canvas, void* model) {
    LessonViewModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    uint8_t f = m->frame;
    if(f >= LESSON_FRAMES) f = 0;
    const LessonFrame* frame = &FRAMES[f];

    /* Header. */
    canvas_draw_box(canvas, 0, 0, 128, 12);
    canvas_invert_color(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 3, 9, frame->title);
    char pos[10];
    snprintf(pos, sizeof(pos), "%u/%u", (unsigned)(f + 1), LESSON_FRAMES);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 125, 9, AlignRight, AlignBottom, pos);
    canvas_invert_color(canvas);

    canvas_set_font(canvas, FontSecondary);
    frame->draw(canvas, m->tick);

    /* Baseline 63 is the last usable row: 64 would clip the descenders off the
     * bottom of the display. */
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignBottom, frame->line1);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, frame->line2);
}

static bool lesson_view_input(InputEvent* event, void* context) {
    LessonView* view = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool consumed = false;
    if(event->key == InputKeyRight || event->key == InputKeyOk) {
        with_view_model(
            view->view,
            LessonViewModel * m,
            {
                if(m->frame + 1 < LESSON_FRAMES) m->frame++;
                m->tick = 0;
            },
            true);
        consumed = true;
    } else if(event->key == InputKeyLeft) {
        /* Left on the first panel falls through to Back, so the lesson never
         * becomes a room you cannot walk out of the way you came in. */
        bool at_start = false;
        with_view_model(
            view->view, LessonViewModel * m, { at_start = (m->frame == 0); }, false);
        if(!at_start) {
            with_view_model(
                view->view,
                LessonViewModel * m,
                {
                    m->frame--;
                    m->tick = 0;
                },
                true);
            consumed = true;
        }
    }
    return consumed;
}

LessonView* lesson_view_alloc(void) {
    LessonView* view = malloc(sizeof(LessonView));
    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(LessonViewModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, lesson_view_draw);
    view_set_input_callback(view->view, lesson_view_input);
    return view;
}

void lesson_view_free(LessonView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* lesson_view_get_view(LessonView* view) {
    furi_assert(view);
    return view->view;
}

void lesson_view_reset(LessonView* view) {
    furi_assert(view);
    with_view_model(
        view->view,
        LessonViewModel * m,
        {
            m->frame = 0;
            m->tick = 0;
        },
        true);
}

void lesson_view_tick(LessonView* view) {
    furi_assert(view);
    with_view_model(
        view->view, LessonViewModel * m, { m->tick++; }, true);
}
