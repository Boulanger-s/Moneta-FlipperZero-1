#!/usr/bin/env python3
"""Render Moneta's 128x64 screens for the README.

These are mock-ups, not device captures, so they are only worth anything if
they are faithful. Every coordinate below is copied from the matching view in
views/, and text is positioned by BASELINE (PIL anchor "ls"/"rs"/"ms"), because
canvas_draw_str takes y as the baseline and getting that wrong is exactly how a
mock-up drifts away from the thing it depicts.

    python3 tools_gen_mockups.py
"""

import os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

W, H = 128, 64
SCALE = 4

# Flipper's orange backlight, and the dark pixels on it.
LCD_BG = (255, 168, 40)
LCD_FG = (26, 21, 12)
BEZEL = (30, 32, 40)
BEZEL_EDGE = (58, 62, 74)

MONO = "/System/Library/Fonts/Supplemental/Menlo.ttc"

# Menlo at 10px advances exactly 6px per character, which lines up with the
# Flipper's own 21-column grid. The secondary font on device is narrower, so it
# gets 8px here — close enough that a collision on screen is a collision here.
F_PRIMARY = ImageFont.truetype(MONO, 10)
F_SECONDARY = ImageFont.truetype(MONO, 8)


def new_screen():
    return Image.new("RGB", (W, H), LCD_BG)


def txt(d, x, y, s, font=F_SECONDARY, anchor="ls", fill=LCD_FG):
    """Draw text by baseline, the way canvas_draw_str does."""
    d.text((x, y), s, font=font, fill=fill, anchor=anchor)


def rframe(d, x, y, w, h, r=2, fill=None):
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, outline=LCD_FG, fill=fill)


def header_bar(d, title, right=None):
    """The inverted 12px header the views draw with canvas_draw_box + invert."""
    d.rectangle([0, 0, W - 1, 11], fill=LCD_FG)
    txt(d, 3, 9, title, F_PRIMARY, fill=LCD_BG)
    if right:
        txt(d, 125, 9, right, F_SECONDARY, anchor="rs", fill=LCD_BG)


# --------------------------------------------------------------- screens


def screen_idle():
    """views/scan_view.c: draw_idle()"""
    img = new_screen()
    d = ImageDraw.Draw(img)

    txt(d, 64, 9, "Hold your card here", F_PRIMARY, anchor="ms")

    # draw_card_glyph(canvas, 6, 20)
    x, y = 6, 20
    rframe(d, x, y, 40, 26, 3)
    d.line([x + 3, y + 5, x + 36, y + 5], fill=LCD_FG)
    rframe(d, x + 5, y + 11, 11, 9, 1)
    d.line([x + 5, y + 15, x + 15, y + 15], fill=LCD_FG)
    d.line([x + 10, y + 11, x + 10, y + 19], fill=LCD_FG)
    d.line([x + 20, y + 17, x + 35, y + 17], fill=LCD_FG)
    d.line([x + 20, y + 20, x + 30, y + 20], fill=LCD_FG)

    # draw_pulse(canvas, 50, 33, tick) — one frozen frame of the three arcs
    for radius in (8, 12, 16):
        d.arc([50 - radius, 33 - radius, 50 + radius, 33 + radius], -50, 50, fill=LCD_FG)

    txt(d, 64, 62, "Contactless bank card", F_SECONDARY, anchor="ms")
    return img


def screen_reading():
    """views/scan_view.c: draw_reading()"""
    img = new_screen()
    d = ImageDraw.Draw(img)

    header_bar(d, "READING", "8 cmds")

    labels = ["Payment directory", "Card application", "Records", "Spending log"]
    done = [True, True, False, False]
    active = [False, False, True, False]
    counts = ["", "", "3", ""]

    for i, label in enumerate(labels):
        y = 23 + i * 11
        cy = y - 3
        if done[i]:
            d.ellipse([6 - 3, cy - 3, 6 + 3, cy + 3], fill=LCD_FG)
        elif active[i]:
            d.ellipse([6 - 3, cy - 3, 6 + 3, cy + 3], outline=LCD_FG)
            d.point((6, cy), fill=LCD_FG)
        else:
            d.ellipse([6 - 2, cy - 2, 6 + 2, cy + 2], outline=LCD_FG)
        txt(d, 14, y, label)
        if counts[i]:
            txt(d, 125, y, counts[i], anchor="rs")

    return img


def _card_body(d, revealed=False, pan="4761739001010119", exp="09/28", right="Visa"):
    """views/card_view.c: the card frame, the number, the expiry row."""
    CARD_X, CARD_Y, CARD_W, CARD_H = 2, 14, 124, 33
    PAN_BASELINE, PAN_LEFT = 32, 8
    rframe(d, CARD_X, CARD_Y, CARD_W, CARD_H, 3)

    if revealed:
        grouped = " ".join(pan[i : i + 4] for i in range(0, len(pan), 4))
        txt(d, PAN_LEFT - 2, PAN_BASELINE, grouped, F_SECONDARY)
    else:
        hidden = len(pan) - 4
        avail = CARD_W - (PAN_LEFT - CARD_X) - 30 - 8
        spacing = max(4, min(6, avail // hidden))
        x = PAN_LEFT
        for i in range(hidden):
            if i > 0 and i % 4 == 0:
                x += 4
            d.ellipse([x, PAN_BASELINE - 4, x + 2, PAN_BASELINE - 2], fill=LCD_FG)
            x += spacing
        x += 4
        txt(d, x, PAN_BASELINE, pan[-4:], F_PRIMARY)

    txt(d, PAN_LEFT - 2, 43, f"EXP {exp}", F_SECONDARY)
    txt(d, 122, 43, right, F_SECONDARY, anchor="rs")


def _grade_badge(d, grade):
    d.rounded_rectangle([106, 0, 126, 12], radius=2, fill=LCD_FG)
    txt(d, 116, 10, grade, F_PRIMARY, anchor="ms", fill=LCD_BG)


def _exposure_bar(d, score):
    x, y, w, h = 4, 48, 76, 7
    rframe(d, x, y, w, h, 1)
    fill = score * (w - 4) // 100
    if fill:
        d.rectangle([x + 2, y + 2, x + 2 + fill - 1, y + h - 3], fill=LCD_FG)
    txt(d, 126, y + 6, f"{score}% out", F_SECONDARY, anchor="rs")


def screen_result():
    """views/card_view.c: the ordinary high-street debit card, graded D."""
    img = new_screen()
    d = ImageDraw.Draw(img)

    txt(d, 2, 10, "VISA DEBIT", F_PRIMARY)
    _grade_badge(d, "D")
    _card_body(d)
    _exposure_bar(d, 61)
    txt(d, 64, 63, "OK: what leaked", F_SECONDARY, anchor="ms")
    return img


def screen_reveal():
    """The same card while OK is held down."""
    img = new_screen()
    d = ImageDraw.Draw(img)

    txt(d, 2, 10, "VISA DEBIT", F_PRIMARY)
    _grade_badge(d, "D")
    _card_body(d, revealed=True)
    _exposure_bar(d, 61)
    txt(d, 64, 63, "Release to hide", F_SECONDARY, anchor="ms")
    return img


def screen_result_f():
    """The talkative credit card: a name on the front, and an F."""
    img = new_screen()
    d = ImageDraw.Draw(img)

    txt(d, 2, 10, "MASTERCARD", F_PRIMARY)
    _grade_badge(d, "F")
    _card_body(d, pan="5413339000001513", exp="11/27", right="R PATEL")
    _exposure_bar(d, 93)
    txt(d, 64, 63, "Right: spending log", F_SECONDARY, anchor="ms")
    return img


def screen_fields():
    """scenes/moneta_scene_fields.c — the firmware Submenu module."""
    img = new_screen()
    d = ImageDraw.Draw(img)

    txt(d, 64, 10, "Leaked: 7 of 11", F_PRIMARY, anchor="ms")
    d.line([0, 13, W - 1, 13], fill=LCD_FG)

    items = ["Card number", "Expiry date", "Your name", "Spending history"]
    for i, item in enumerate(items):
        y = 16 + i * 12
        if i == 0:
            d.rounded_rectangle([1, y, W - 2, y + 11], radius=3, fill=LCD_FG)
            txt(d, 64, y + 9, item, F_PRIMARY, anchor="ms", fill=LCD_BG)
        else:
            txt(d, 64, y + 9, item, F_PRIMARY, anchor="ms")
    return img


def screen_explain():
    """scenes/moneta_scene_explain.c — the scrolling widget."""
    img = new_screen()
    d = ImageDraw.Draw(img)

    txt(d, 2, 9, "Card number", F_PRIMARY)
    lines = [
        "Worth 35 of 100 on the",
        "exposure score.",
        "",
        "What it is",
        "The long number printed on",
        "the front. The chip hands it",
    ]
    for i, line in enumerate(lines):
        font = F_PRIMARY if line == "What it is" else F_SECONDARY
        txt(d, 2, 20 + i * 8, line, font)

    # the widget's scrollbar
    d.line([126, 12, 126, 63], fill=LCD_FG)
    d.rectangle([124, 12, 127, 30], fill=LCD_FG)
    return img


def screen_log():
    """views/log_view.c"""
    img = new_screen()
    d = ImageDraw.Draw(img)

    header_bar(d, "SPENDING LOG", "1/4")
    txt(d, 4, 25, "14 Jul 2026", F_SECONDARY)
    txt(d, 4, 40, "42.50 INR", F_PRIMARY)
    txt(d, 4, 52, "CAFE COFFEE DAY", F_SECONDARY)
    txt(d, 64, 63, "Up / Down for more", F_SECONDARY, anchor="ms")
    return img


def screen_lesson():
    """views/lesson_view.c: panel 5, panel_limits()"""
    img = new_screen()
    d = ImageDraw.Draw(img)

    header_bar(d, "WHAT IT IS WORTH", "5/5")

    rframe(d, 4, 16, 52, 30, 2)
    d.line([16, 27, 22, 33], fill=LCD_FG)
    d.line([22, 33, 40, 19], fill=LCD_FG)
    txt(d, 30, 42, "checkout", F_SECONDARY, anchor="ms")

    rframe(d, 72, 16, 52, 30, 2)
    d.line([86, 20, 110, 33], fill=LCD_FG)
    d.line([110, 20, 86, 33], fill=LCD_FG)
    txt(d, 98, 42, "shop tap", F_SECONDARY, anchor="ms")

    txt(d, 64, 56, "Enough to type into a", F_SECONDARY, anchor="ms")
    txt(d, 64, 63, "checkout. Not to tap.", F_SECONDARY, anchor="ms")
    return img


# ----------------------------------------------------------------- output


def bezel(img, scale=SCALE, pad=10):
    big = img.resize((W * scale, H * scale), Image.NEAREST)
    out = Image.new("RGB", (W * scale + pad * 2, H * scale + pad * 2), BEZEL)
    d = ImageDraw.Draw(out)
    d.rounded_rectangle([0, 0, out.width - 1, out.height - 1], radius=8, outline=BEZEL_EDGE)
    out.paste(big, (pad, pad))
    return out


SCREENS = [
    ("screen_idle", screen_idle),
    ("screen_reading", screen_reading),
    ("screen_result", screen_result),
    ("screen_reveal", screen_reveal),
    ("screen_result_f", screen_result_f),
    ("screen_fields", screen_fields),
    ("screen_explain", screen_explain),
    ("screen_log", screen_log),
    ("screen_lesson", screen_lesson),
]

STRIP = [
    "screen_idle",
    "screen_reading",
    "screen_result",
    "screen_fields",
    "screen_log",
    "screen_lesson",
]


def main():
    rendered = {}
    for name, fn in SCREENS:
        img = fn()
        rendered[name] = img
        path = os.path.join(OUT, name + ".png")
        bezel(img).save(path)
        print("wrote", path)

    # A single strip for the top of the README.
    tiles = [bezel(rendered[n], scale=3, pad=6) for n in STRIP]
    gap = 10
    cols = 3
    rows = (len(tiles) + cols - 1) // cols
    tw, th = tiles[0].size
    strip = Image.new(
        "RGB",
        (cols * tw + (cols + 1) * gap, rows * th + (rows + 1) * gap),
        (14, 16, 24),
    )
    for i, tile in enumerate(tiles):
        cx = i % cols
        cy = i // cols
        strip.paste(tile, (gap + cx * (tw + gap), gap + cy * (th + gap)))
    path = os.path.join(OUT, "screens.png")
    strip.save(path)
    print("wrote", path)


if __name__ == "__main__":
    main()
