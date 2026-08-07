#include "scan_view.h"

#include <furi.h>
#include <gui/elements.h>

/* Two screens in one view.
 *
 * While there is nothing in the field, it shows a card and a pulse — an
 * invitation. The moment a card answers it becomes a checklist, ticking off
 * the four steps a shop terminal performs, with a running count of the
 * commands sent. That count is the argument: a stranger's reader needs about
 * eight commands and no cooperation from you at all.
 */

#define ARC_NUM 3
#define ARC_PERIOD 12u /* ticks per pulse */

typedef struct {
    EmvReaderProgress progress;
    uint32_t tick;
} ScanViewModel;

struct ScanView {
    View* view;
};

/* A card outline with a chip, drawn rather than shipped as an asset so it can
 * breathe with the animation. */
static void draw_card_glyph(Canvas* canvas, int x, int y) {
    canvas_draw_rframe(canvas, x, y, 40, 26, 3);
    /* magnetic-stripe hint along the top */
    canvas_draw_line(canvas, x + 3, y + 5, x + 36, y + 5);
    /* the chip */
    canvas_draw_rframe(canvas, x + 5, y + 11, 11, 9, 1);
    canvas_draw_line(canvas, x + 5, y + 15, x + 15, y + 15);
    canvas_draw_line(canvas, x + 10, y + 11, x + 10, y + 19);
    /* embossed digits */
    canvas_draw_line(canvas, x + 20, y + 17, x + 35, y + 17);
    canvas_draw_line(canvas, x + 20, y + 20, x + 30, y + 20);
}

/* Concentric arcs travelling away from the card. canvas_draw_arc does not
 * exist in the firmware API, so each arc is a short run of dots along a
 * circle — cheap, and it reads correctly at this size. */
static void draw_pulse(Canvas* canvas, int cx, int cy, uint32_t tick) {
    for(int a = 0; a < ARC_NUM; a++) {
        uint32_t phase = (tick + (uint32_t)a * (ARC_PERIOD / ARC_NUM)) % ARC_PERIOD;
        int radius = 6 + (int)phase;
        /* Fade the arc out as it travels by dropping every other dot. */
        int step = (phase > ARC_PERIOD * 2 / 3) ? 2 : 1;

        for(int i = -5; i <= 5; i += step) {
            /* A quarter-circle sweep to the right of the card. */
            int dy = i * radius / 8;
            int dx2 = radius * radius - dy * dy;
            if(dx2 <= 0) continue;
            int dx = 0;
            while((dx + 1) * (dx + 1) <= dx2) dx++;
            canvas_draw_dot(canvas, cx + dx, cy + dy);
        }
    }
}

static const char* state_title(EmvReaderState state) {
    switch(state) {
    case EmvReaderSelecting:
        return "READING";
    case EmvReaderOpeningApp:
        return "READING";
    case EmvReaderReadingRecords:
        return "READING";
    case EmvReaderReadingLog:
        return "READING";
    case EmvReaderDone:
        return "DONE";
    case EmvReaderLost:
        return "CARD MOVED";
    default:
        return "SEARCHING";
    }
}

/* A step is done once the reader has moved past it. */
static bool step_done(EmvReaderState state, int step) {
    static const EmvReaderState order[] = {
        EmvReaderSelecting,
        EmvReaderOpeningApp,
        EmvReaderReadingRecords,
        EmvReaderReadingLog,
    };
    if(state == EmvReaderDone) return true;
    if(step < 0 || step > 3) return false;
    return (int)state > (int)order[step] && state != EmvReaderLost &&
           state != EmvReaderNotPayment;
}

static bool step_active(EmvReaderState state, int step) {
    static const EmvReaderState order[] = {
        EmvReaderSelecting,
        EmvReaderOpeningApp,
        EmvReaderReadingRecords,
        EmvReaderReadingLog,
    };
    if(step < 0 || step > 3) return false;
    return state == order[step];
}

static void draw_idle(Canvas* canvas, const ScanViewModel* m) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 9, AlignCenter, AlignBottom, "Hold your card here");

    draw_card_glyph(canvas, 6, 20);
    draw_pulse(canvas, 50, 33, m->tick);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Contactless bank card");
}

static void draw_not_payment(Canvas* canvas, const ScanViewModel* m) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignBottom, "Not a payment card");

    canvas_set_font(canvas, FontSecondary);
    const char* what = m->progress.other_card[0] ? m->progress.other_card : "Unknown card";
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignBottom, what);
    canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignBottom, "Nothing to skim here.");
    canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignBottom, "Try a contactless card.");
}

static void draw_reading(Canvas* canvas, const ScanViewModel* m) {
    const EmvReaderProgress* p = &m->progress;

    /* Header bar: what we are doing, and what it is costing in commands. */
    canvas_draw_box(canvas, 0, 0, 128, 12);
    canvas_invert_color(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 3, 9, state_title(p->state));

    char cmds[16];
    snprintf(cmds, sizeof(cmds), "%u cmds", (unsigned)p->apdus);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 125, 9, AlignRight, AlignBottom, cmds);
    canvas_invert_color(canvas);

    static const char* const labels[4] = {
        "Payment directory",
        "Card application",
        "Records",
        "Spending log",
    };

    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < 4; i++) {
        int y = 23 + i * 11;

        bool done = step_done(p->state, i);
        bool active = step_active(p->state, i);

        if(done) {
            canvas_draw_disc(canvas, 6, y - 3, 3);
        } else if(active) {
            /* A pulsing ring, so a slow step never looks like a hung one. */
            canvas_draw_circle(canvas, 6, y - 3, 3);
            if((m->tick / 3) % 2) canvas_draw_dot(canvas, 6, y - 3);
        } else {
            canvas_draw_circle(canvas, 6, y - 3, 2);
        }

        canvas_draw_str(canvas, 14, y, labels[i]);

        /* Counts, once there is something to count. */
        char n[12] = {0};
        if(i == 2 && p->records > 0) snprintf(n, sizeof(n), "%u", (unsigned)p->records);
        if(i == 3 && p->log_entries > 0) snprintf(n, sizeof(n), "%u", (unsigned)p->log_entries);
        if(n[0]) canvas_draw_str_aligned(canvas, 125, y, AlignRight, AlignBottom, n);
    }
}

static void scan_view_draw(Canvas* canvas, void* model) {
    ScanViewModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    switch(m->progress.state) {
    case EmvReaderNotPayment:
        draw_not_payment(canvas, m);
        break;
    case EmvReaderIdle:
    case EmvReaderSearching:
        draw_idle(canvas, m);
        break;
    default:
        draw_reading(canvas, m);
        break;
    }
}

ScanView* scan_view_alloc(void) {
    ScanView* view = malloc(sizeof(ScanView));
    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(ScanViewModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, scan_view_draw);
    return view;
}

void scan_view_free(ScanView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* scan_view_get_view(ScanView* view) {
    furi_assert(view);
    return view->view;
}

void scan_view_set_progress(ScanView* view, const EmvReaderProgress* progress) {
    furi_assert(view);
    with_view_model(
        view->view, ScanViewModel * m, { m->progress = *progress; }, true);
}

void scan_view_tick(ScanView* view) {
    furi_assert(view);
    with_view_model(
        view->view, ScanViewModel * m, { m->tick++; }, true);
}
