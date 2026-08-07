#include "log_view.h"

#include <furi.h>
#include <gui/elements.h>

#include "text_fit.h"

/* The spending log, one purchase to a screen.
 *
 * A scrolling list would make this look like data. One transaction at a time,
 * in a large font, reads like a receipt someone else is holding — which is
 * precisely what it is.
 */

typedef struct {
    EmvCard card;
    uint8_t index;
} LogViewModel;

struct LogView {
    View* view;
};

static const char* const MONTHS[13] = {
    "???",
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec",
};

/* Most currencies split into hundredths. The yen and the won do not, and
 * printing "4250.00 JPY" for a 4250-yen coffee would be wrong rather than
 * merely ugly. */
static uint8_t currency_exponent(uint16_t code) {
    switch(code) {
    case 392: /* JPY */
    case 410: /* KRW */
    case 704: /* VND */
        return 0;
    default:
        return 2;
    }
}

static void format_amount(const EmvLogEntry* e, char* out, size_t out_len) {
    const char* cur = e->has_currency ? emv_currency_name(e->currency) : NULL;

    if(!e->has_amount) {
        snprintf(out, out_len, "amount not logged");
        return;
    }

    if(currency_exponent(e->has_currency ? e->currency : 0) == 0) {
        if(cur) {
            snprintf(out, out_len, "%lu %s", (unsigned long)e->amount, cur);
        } else {
            snprintf(out, out_len, "%lu", (unsigned long)e->amount);
        }
        return;
    }

    unsigned long major = (unsigned long)(e->amount / 100u);
    unsigned minor = (unsigned)(e->amount % 100u);
    if(cur) {
        snprintf(out, out_len, "%lu.%02u %s", major, minor, cur);
    } else {
        snprintf(out, out_len, "%lu.%02u", major, minor);
    }
}

static void log_view_draw(Canvas* canvas, void* model) {
    LogViewModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(m->card.log_num == 0) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignBottom, "No log on this card");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 42, AlignCenter, AlignBottom, "Good. Many cards keep one.");
        return;
    }

    uint8_t idx = m->index;
    if(idx >= m->card.log_num) idx = 0;
    const EmvLogEntry* e = &m->card.log[idx];

    /* Header: what this is and where you are in it. */
    canvas_draw_box(canvas, 0, 0, 128, 12);
    canvas_invert_color(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 3, 9, "SPENDING LOG");
    char pos[12];
    snprintf(pos, sizeof(pos), "%u/%u", (unsigned)(idx + 1), (unsigned)m->card.log_num);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 125, 9, AlignRight, AlignBottom, pos);
    canvas_invert_color(canvas);

    /* Date. */
    canvas_set_font(canvas, FontSecondary);
    if(e->has_date) {
        char date[24];
        const char* month = MONTHS[(e->month >= 1 && e->month <= 12) ? e->month : 0];
        snprintf(date, sizeof(date), "%u %s 20%02u", (unsigned)e->day, month, (unsigned)e->year);
        canvas_draw_str(canvas, 4, 25, date);
    } else {
        canvas_draw_str(canvas, 4, 25, "date not logged");
    }

    /* Amount, in the biggest font the screen has. */
    char amount[32];
    format_amount(e, amount, sizeof(amount));
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 40, amount);

    /* Where. This is the line that unsettles people. */
    canvas_set_font(canvas, FontSecondary);
    if(e->has_merchant) {
        char merchant[MONETA_MERCHANT_MAX + 1];
        strncpy(merchant, e->merchant, sizeof(merchant) - 1);
        merchant[sizeof(merchant) - 1] = '\0';
        text_fit_width(canvas, merchant, 120);
        canvas_draw_str(canvas, 4, 52, merchant);
    } else if(e->has_country) {
        const char* country = emv_country_name(e->country);
        canvas_draw_str(canvas, 4, 52, country ? country : "merchant not logged");
    }

    if(m->card.log_num > 1) {
        canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "Up / Down for more");
    }
}

static bool log_view_input(InputEvent* event, void* context) {
    LogView* view = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool consumed = false;
    if(event->key == InputKeyDown) {
        with_view_model(
            view->view,
            LogViewModel * m,
            {
                if(m->card.log_num > 0) m->index = (uint8_t)((m->index + 1) % m->card.log_num);
            },
            true);
        consumed = true;
    } else if(event->key == InputKeyUp) {
        with_view_model(
            view->view,
            LogViewModel * m,
            {
                if(m->card.log_num > 0) {
                    m->index = (uint8_t)((m->index + m->card.log_num - 1) % m->card.log_num);
                }
            },
            true);
        consumed = true;
    }
    return consumed;
}

LogView* log_view_alloc(void) {
    LogView* view = malloc(sizeof(LogView));
    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(LogViewModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, log_view_draw);
    view_set_input_callback(view->view, log_view_input);
    return view;
}

void log_view_free(LogView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* log_view_get_view(LogView* view) {
    furi_assert(view);
    return view->view;
}

void log_view_set_card(LogView* view, const EmvCard* card) {
    furi_assert(view);
    with_view_model(
        view->view,
        LogViewModel * m,
        {
            m->card = *card;
            m->index = 0;
        },
        true);
}
