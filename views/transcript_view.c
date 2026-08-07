#include "transcript_view.h"

#include <furi.h>
#include <gui/elements.h>

/* The actual conversation, one exchange to a screen.
 *
 * Everything else in Moneta is an interpretation of these bytes. This screen is
 * the bytes. It is here so that nobody has to take the grade on trust, and so
 * that a card which behaves strangely can be diagnosed by the person holding
 * it rather than by attaching a debugger.
 */

#define HEX_PER_ROW 8
#define ROW_CMD_1 21
#define ROW_CMD_2 29
#define ROW_RESP_1 40
#define ROW_RESP_2 48
#define ROW_RESP_3 56

typedef struct {
    EmvTranscript trx;
    uint8_t index;
} TranscriptViewModel;

struct TranscriptView {
    View* view;
};

/* "A0 00 00 00 03 10 10 00" — eight bytes is twenty-three characters, which is
 * the widest a FontSecondary row fits on a 128-pixel screen. */
static void hex_row(const uint8_t* data, size_t len, size_t offset, char* out, size_t out_len) {
    out[0] = '\0';
    size_t pos = 0;
    for(size_t i = 0; i < HEX_PER_ROW && offset + i < len; i++) {
        int n = snprintf(out + pos, out_len - pos, (i == 0) ? "%02X" : " %02X", data[offset + i]);
        if(n <= 0 || (size_t)n >= out_len - pos) break;
        pos += (size_t)n;
    }
}

/* A solid marker for what we sent, a hollow one for what came back. */
static void direction_marker(Canvas* canvas, int y, bool outbound) {
    if(outbound) {
        canvas_draw_box(canvas, 0, y - 5, 4, 4);
    } else {
        canvas_draw_frame(canvas, 0, y - 5, 4, 4);
    }
}

static void draw_empty(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignBottom, "No transcript");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignBottom, "Read a card first.");
}

static void transcript_view_draw(Canvas* canvas, void* model) {
    TranscriptViewModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(m->trx.num == 0) {
        draw_empty(canvas);
        return;
    }

    uint8_t idx = m->index;
    if(idx >= m->trx.num) idx = 0;
    const EmvApduEntry* e = &m->trx.entry[idx];

    /* Header: which exchange, what it was, and how the card answered. */
    canvas_draw_box(canvas, 0, 0, 128, 12);
    canvas_invert_color(canvas);

    char pos[24];
    snprintf(pos, sizeof(pos), "%u/%u", (unsigned)(idx + 1), (unsigned)m->trx.num);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 9, pos);
    canvas_draw_str(canvas, 26, 9, e->label);

    char sw[10];
    if(e->failed) {
        snprintf(sw, sizeof(sw), "no ans");
    } else {
        snprintf(sw, sizeof(sw), "%04X", (unsigned)e->sw);
    }
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, sw);
    canvas_invert_color(canvas);

    canvas_set_font(canvas, FontSecondary);
    char row[32];

    /* What we sent. */
    direction_marker(canvas, ROW_CMD_1, true);
    hex_row(e->cmd, e->cmd_stored, 0, row, sizeof(row));
    canvas_draw_str(canvas, 6, ROW_CMD_1, row);
    if(e->cmd_stored > HEX_PER_ROW) {
        hex_row(e->cmd, e->cmd_stored, HEX_PER_ROW, row, sizeof(row));
        canvas_draw_str(canvas, 6, ROW_CMD_2, row);
    }
    if(e->cmd_len > e->cmd_stored) {
        snprintf(row, sizeof(row), "+%u more", (unsigned)(e->cmd_len - e->cmd_stored));
        canvas_draw_str_aligned(canvas, 126, ROW_CMD_2, AlignRight, AlignBottom, row);
    }

    /* What came back. */
    if(e->failed) {
        canvas_draw_str(canvas, 6, ROW_RESP_1, "card did not answer");
        return;
    }

    direction_marker(canvas, ROW_RESP_1, false);
    if(e->resp_stored == 0) {
        canvas_draw_str(canvas, 6, ROW_RESP_1, "(status word only)");
    } else {
        hex_row(e->resp, e->resp_stored, 0, row, sizeof(row));
        canvas_draw_str(canvas, 6, ROW_RESP_1, row);
        if(e->resp_stored > HEX_PER_ROW) {
            hex_row(e->resp, e->resp_stored, HEX_PER_ROW, row, sizeof(row));
            canvas_draw_str(canvas, 6, ROW_RESP_2, row);
        }
        if(e->resp_stored > HEX_PER_ROW * 2) {
            hex_row(e->resp, e->resp_stored, HEX_PER_ROW * 2, row, sizeof(row));
            canvas_draw_str(canvas, 6, ROW_RESP_3, row);
        }
    }

    /* Be explicit about what was cut, rather than quietly showing a prefix. */
    if(e->resp_len > e->resp_stored) {
        snprintf(row, sizeof(row), "%u bytes total", (unsigned)e->resp_len);
        canvas_draw_str_aligned(canvas, 126, 63, AlignRight, AlignBottom, row);
    }
}

static bool transcript_view_input(InputEvent* event, void* context) {
    TranscriptView* view = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool consumed = false;
    if(event->key == InputKeyDown || event->key == InputKeyRight) {
        with_view_model(
            view->view,
            TranscriptViewModel * m,
            {
                if(m->trx.num > 0) m->index = (uint8_t)((m->index + 1) % m->trx.num);
            },
            true);
        consumed = true;
    } else if(event->key == InputKeyUp) {
        with_view_model(
            view->view,
            TranscriptViewModel * m,
            {
                if(m->trx.num > 0) m->index = (uint8_t)((m->index + m->trx.num - 1) % m->trx.num);
            },
            true);
        consumed = true;
    }
    return consumed;
}

TranscriptView* transcript_view_alloc(void) {
    TranscriptView* view = malloc(sizeof(TranscriptView));
    view->view = view_alloc();
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(TranscriptViewModel));
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, transcript_view_draw);
    view_set_input_callback(view->view, transcript_view_input);
    return view;
}

void transcript_view_free(TranscriptView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* transcript_view_get_view(TranscriptView* view) {
    furi_assert(view);
    return view->view;
}

void transcript_view_set(TranscriptView* view, const EmvTranscript* transcript) {
    furi_assert(view);
    with_view_model(
        view->view,
        TranscriptViewModel * m,
        {
            m->trx = *transcript;
            m->index = 0;
        },
        true);
}
