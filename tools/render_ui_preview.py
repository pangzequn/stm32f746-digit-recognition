"""Render a 2x preview of the STM32F746 UI layout (approximate fonts).

Layout constants mirror Src/ui.c: left pad + right CONFIDENCE / RESULT panels,
both spanning y=6..261, header bars 27px thick.
"""
from PIL import Image, ImageDraw, ImageFont

S = 2  # scale
W, H = 480 * S, 272 * S

img = Image.new("RGB", (W, H), "white")
d = ImageDraw.Draw(img)

BLUE      = (0, 0, 255)
DARKBLUE  = (0, 0, 128)
GRAY      = (128, 128, 128)
LIGHTGRAY = (211, 211, 211)
ORANGE    = (255, 165, 0)
DARKGRAY  = (64, 64, 64)

PANEL_H = 255  # DrawRect height, bottom edge lands on y = 261


def font(px):
    try:
        return ImageFont.truetype("C:/Windows/Fonts/consola.ttf", px * S)
    except Exception:
        return ImageFont.load_default()

F12, F16 = font(11), font(14)
BIG = font(125)  # ~96px cap height at 1x, matches Font24 x4


def fill(x, y, w, h, c):
    if w <= 0 or h <= 0:
        return
    d.rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], fill=c)


def rect(x, y, w, h, c):
    d.rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], outline=c)


def text(x, y, s, f, c):
    d.text((x * S, y * S), s, font=f, fill=c)


def ctext(cx, y, s, f, c):
    w = d.textlength(s, font=f)
    d.text((cx * S - w / 2, y * S), s, font=f, fill=c)


# ---------------- left pad ----------------
fill(8, 6, 229, 27, BLUE)
ctext(122.5, 11, "WRITE A DIGIT", F16, "white")
rect(8, 33, 228, 228, BLUE)
rect(9, 34, 226, 226, BLUE)
rect(10, 35, 224, 224, BLUE)

# ---------------- panels (y=6..261, header 27) ----------------
for px, pw, title in ((242, 144, "CONFIDENCE"), (390, 86, "RESULT")):
    fill(px, 6, pw, PANEL_H, "white")
    rect(px, 6, pw, PANEL_H, GRAY)
    fill(px, 6, pw, 27, DARKBLUE)
    ctext(px + pw / 2, 13, title, F12, "white")

pct = [4, 1, 9, 2, 0, 0, 3, 97, 2, 0]
best = 7
for i, p in enumerate(pct):
    y = 42 + i * 22
    fill(249, y, 9, 12, "white")
    text(249, y, str(i), F12, ORANGE if i == best else DARKGRAY)
    if i == best:
        text(250, y, str(i), F12, ORANGE)
    fill(259, y, 86, 12, LIGHTGRAY)
    if p:
        fill(259, y, 86 * p // 100, 12, ORANGE if i == best else BLUE)
    text(348, y, f"{p:3d}%", F12, ORANGE if i == best else DARKGRAY)

# result card
fill(395, 39, 77, 103, "white")
ctext(394 + 39, 42, "7", BIG, "black")
rect(394, 38, 78, 104, GRAY)
ctext(394 + 39, 152, "97%", F12, BLUE)

# buttons
fill(394, 172, 78, 32, BLUE)
ctext(394 + 39, 182, "SCAN", F12, "white")
fill(394, 211, 78, 32, "white")
rect(394, 211, 78, 32, BLUE)
ctext(394 + 39, 221, "CLEAR", F12, BLUE)
ctext(390 + 43, 248, "WRITING", F12, DARKGRAY)

# sample handwriting
pts = [(120, 90), (150, 80), (170, 120), (130, 160), (175, 175), (110, 200)]
for a, b in zip(pts, pts[1:]):
    d.line([a[0] * S, a[1] * S, b[0] * S, b[1] * S], fill="black", width=3 * S)

img.save("docs/ui_layout_preview.png")
print("saved", img.size)
