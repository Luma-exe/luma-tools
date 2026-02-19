"""
Generate public/og-image.png  (1200 × 630)
Matches Luma Tools site aesthetic: dark bg, purple/cyan/pink accents.
Run from repo root:  py tools/gen_og_image.py
"""
from PIL import Image, ImageDraw, ImageFont
import os

W, H = 1200, 630
HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, '..', 'public', 'og-image.png')

BG          = (10, 10, 15)
ACCENT      = (124, 92, 255)
ACCENT_CYAN = (0, 212, 255)
ACCENT_PINK = (255, 107, 202)
TEXT_PRI    = (240, 240, 245)
TEXT_MUT    = (136, 136, 160)

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def blend(fg, bg, alpha):
    return tuple(int(fg[i] * alpha + bg[i] * (1 - alpha)) for i in range(3))

def make_font(size, bold=False, semi=False):
    cands = []
    if bold:   cands = ['C:/Windows/Fonts/segoeuib.ttf',  'C:/Windows/Fonts/arialbd.ttf']
    elif semi: cands = ['C:/Windows/Fonts/segoeuisb.ttf', 'C:/Windows/Fonts/segoeuib.ttf', 'C:/Windows/Fonts/arialbd.ttf']
    else:      cands = ['C:/Windows/Fonts/segoeui.ttf',   'C:/Windows/Fonts/arial.ttf']
    for p in cands:
        if os.path.exists(p): return ImageFont.truetype(p, size)
    return ImageFont.load_default()

def draw_orb(layer, cx, cy, radius, colour, max_alpha=85, steps=60):
    d = ImageDraw.Draw(layer)
    r, g, b = colour
    for i in range(steps, 0, -1):
        frac = i / steps
        d.ellipse([cx - int(radius*frac)] * 2 + [cx + int(radius*frac)] * 2,
                  fill=(r, g, b, int(max_alpha * (frac ** 1.9))))

base = Image.new('RGBA', (W, H), BG + (255,))
orbs = Image.new('RGBA', (W, H), (0, 0, 0, 0))
draw_orb(orbs, 1090, -70, 540, ACCENT,      75)
draw_orb(orbs,  -70, 620, 440, ACCENT_CYAN, 65)
draw_orb(orbs,  600, 400, 300, ACCENT_PINK, 18)
base = Image.alpha_composite(base, orbs)

dots = Image.new('RGBA', (W, H), (0, 0, 0, 0))
dd   = ImageDraw.Draw(dots)
for x in range(0, W + 38, 38):
    for y in range(0, H + 38, 38):
        dd.ellipse([x-1, y-1, x+1, y+1], fill=(255, 255, 255, 16))
base = Image.alpha_composite(base, dots)

img  = base.convert('RGB')
draw = ImageDraw.Draw(img)

f_logo     = make_font(80, bold=True)
f_sub      = make_font(32, semi=True)
f_badge    = make_font(21, semi=True)
f_url      = make_font(26)
f_stat_val = make_font(34, bold=True)
f_stat_lbl = make_font(20)

TX, TY = 100, 138
draw.text((TX+3, TY+3), 'Luma Tools', font=f_logo, fill=BG)
luma_w = int(draw.textlength('Luma ', font=f_logo))
draw.text((TX,          TY), 'Luma ',  font=f_logo, fill=ACCENT)
draw.text((TX + luma_w, TY), 'Tools',  font=f_logo, fill=TEXT_PRI)
draw.text((TX, TY + 92), 'All-in-One Browser Toolkit', font=f_sub, fill=TEXT_MUT)

DIV_Y = TY + 142
for i in range(680):
    t = i / 680
    c = lerp(ACCENT, ACCENT_CYAN, t*2) if t < 0.5 else lerp(ACCENT_CYAN, ACCENT_PINK, (t-0.5)*2)
    draw.rectangle([TX+i, DIV_Y, TX+i+1, DIV_Y+2], fill=c)

cats = [
    ('AI Study Tools', ACCENT),
    ('Image Tools',    (80, 200, 120)),
    ('Video Tools',    (255, 160, 50)),
    ('Audio Tools',    ACCENT_CYAN),
    ('PDF Tools',      ACCENT_PINK),
    ('Downloader',     (255, 90, 90)),
    ('Utilities',      (170, 170, 200)),
]
px, py = TX, DIV_Y + 24
for label, colour in cats:
    pw = int(draw.textlength(label, font=f_badge)) + 32
    x0, y0, x1, y1 = px, py, px + pw, py + 36
    draw.rounded_rectangle([x0, y0, x1, y1], radius=18, fill=blend(colour, BG, 0.18))
    draw.rounded_rectangle([x0, y0, x1, y1], radius=18, outline=blend(colour, (0,0,0), 0.75), width=1)
    draw.text((x0 + 16, y0 + 7), label, font=f_badge, fill=colour)
    px += pw + 10
    if px > W - 180:
        px = TX; py += 44

STAT_Y = py + 52
sx = TX
for val, lbl in [('45+', 'Free Tools'), ('No Upload Needed', 'for most tools'), ('100%', 'Private')]:
    draw.text((sx, STAT_Y), val, font=f_stat_val, fill=ACCENT)
    vw = int(draw.textlength(val, font=f_stat_val))
    draw.text((sx + vw + 8, STAT_Y + 8), lbl, font=f_stat_lbl, fill=TEXT_MUT)
    sx += vw + 8 + int(draw.textlength(lbl, font=f_stat_lbl)) + 44

BY = H - 56
draw.rectangle([0, BY, W, H], fill=blend(BG, (0,0,0), 0.6))
for i in range(W):
    draw.rectangle([i, BY, i+1, BY+1], fill=lerp(ACCENT, ACCENT_CYAN, i/W))
url = 'tools.lumaplayground.com'
uw  = int(draw.textlength(url, font=f_url))
draw.text(((W - uw) // 2, BY + 14), url, font=f_url, fill=TEXT_MUT)

img.save(OUT, 'PNG', optimize=True)
print(f'Saved: {os.path.abspath(OUT)}')


# ── colours ──────────────────────────────────────────────────────────────────
BG          = (10, 10, 15)
ACCENT      = (124, 92, 255)   # --accent  #7c5cff
ACCENT_CYAN = (0, 212, 255)    # --accent-2 #00d4ff
ACCENT_PINK = (255, 107, 202)  # --accent-3 #ff6bca
TEXT_PRI    = (240, 240, 245)
TEXT_MUT    = (136, 136, 160)
CARD_BG     = (18, 18, 30)

# ── font loader ───────────────────────────────────────────────────────────────
def font(size, bold=False, semibold=False):
    candidates = []
    if bold:
        candidates = [
            'C:/Windows/Fonts/segoeuib.ttf',
            'C:/Windows/Fonts/arialbd.ttf',
        ]
    elif semibold:
        candidates = [
            'C:/Windows/Fonts/segoeuisb.ttf',
            'C:/Windows/Fonts/segoeuib.ttf',
            'C:/Windows/Fonts/arialbd.ttf',
        ]
    else:
        candidates = [
            'C:/Windows/Fonts/segoeui.ttf',
            'C:/Windows/Fonts/arial.ttf',
        ]
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()

# ── radial glow helper (RGBA layer) ──────────────────────────────────────────
def draw_orb(layer, cx, cy, radius, colour, max_alpha=90, steps=60):
    d = ImageDraw.Draw(layer)
    r, g, b = colour
    for i in range(steps, 0, -1):
        frac  = i / steps
        rad   = int(radius * frac)
        alpha = int(max_alpha * (frac ** 1.8))
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad],
                  fill=(r, g, b, alpha))

# ── rounded rect helper ───────────────────────────────────────────────────────
def rounded_rect(draw, box, radius, fill=None, outline=None, width=2):
    x0, y0, x1, y1 = box
    draw.rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=fill,
                           outline=outline, width=width)

# ─────────────────────────────────────────────────────────────────────────────
img = Image.new('RGB', (W, H), BG)

# ── glowing orbs ─────────────────────────────────────────────────────────────
orb_layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
draw_orb(orb_layer, 1080, -60,  520, ACCENT,      max_alpha=80)
draw_orb(orb_layer, -60,  600,  420, ACCENT_CYAN,  max_alpha=70)
draw_orb(orb_layer, 610,  380,  320, ACCENT_PINK,  max_alpha=22)
img.paste(Image.new('RGB', (W, H), BG), mask=None)
img = Image.alpha_composite(img.convert('RGBA'), orb_layer).convert('RGB')

# ── subtle grid dots ─────────────────────────────────────────────────────────
grid = Image.new('RGBA', (W, H), (0, 0, 0, 0))
gd   = ImageDraw.Draw(grid)
STEP = 38
for x in range(0, W, STEP):
    for y in range(0, H, STEP):
        gd.ellipse([x-1, y-1, x+1, y+1], fill=(255, 255, 255, 18))
img = Image.alpha_composite(img.convert('RGBA'), grid).convert('RGB')

draw = ImageDraw.Draw(img)

# ── left accent bar ───────────────────────────────────────────────────────────
bar_x = 72
for i in range(6):
    frac = i / 5
    c = tuple(int(ACCENT[ch] + (ACCENT_PINK[ch] - ACCENT[ch]) * frac) for ch in range(3))
    draw.rectangle([bar_x, 145 + i*40, bar_x + 4, 175 + i*40], fill=c)

# ── header: site name ─────────────────────────────────────────────────────────
f_logo   = font(82, bold=True)
f_tag    = font(34, semibold=True)
f_badge  = font(22, semibold=True)
f_url    = font(26)
f_label  = font(19)

# Gradient title: draw "Luma" in accent and "Tools" in white
x_start = 100
title_y  = 138

# Shadow
draw.text((x_start+3, title_y+3), "Luma Tools", font=f_logo, fill=(0, 0, 0, 120))

# Accent "Luma" — draw char by char with slight gradient
luma_w = draw.textlength("Luma ", font=f_logo)
draw.text((x_start, title_y), "Luma ", font=f_logo, fill=ACCENT)
draw.text((x_start + int(luma_w), title_y), "Tools", font=f_logo, fill=TEXT_PRI)

# ── tagline ───────────────────────────────────────────────────────────────────
draw.text((x_start, title_y + 96), "All-in-One Browser Toolkit", font=f_tag, fill=TEXT_MUT)

# ── divider line ──────────────────────────────────────────────────────────────
div_y = title_y + 148
for i in range(680):
    frac = i / 680
    if frac < 0.5:
        c = tuple(int(ACCENT[ch] + (ACCENT_CYAN[ch] - ACCENT[ch]) * (frac*2)) for ch in range(3))
    else:
        c = tuple(int(ACCENT_CYAN[ch] + (ACCENT_PINK[ch] - ACCENT_CYAN[ch]) * ((frac-0.5)*2)) for ch in range(3))
    draw.rectangle([x_start + i, div_y, x_start + i + 1, div_y + 2], fill=c)

# ── tool category pills ───────────────────────────────────────────────────────
categories = [
    ("AI Study Tools",  ACCENT),
    ("Image Tools",     (80, 200, 120)),
    ("Video Tools",     (255, 160, 50)),
    ("Audio Tools",     ACCENT_CYAN),
    ("PDF Tools",       (255, 107, 202)),
    ("Downloader",      (255, 90, 90)),
    ("Utilities",       (170, 170, 200)),
]

pill_x = x_start
pill_y = div_y + 26
pad_x, pad_y, gap = 16, 9, 12

for label, colour in categories:
    tw = int(draw.textlength(label, font=f_badge))
    pw = tw + pad_x * 2
    ph = 36

    # pill background (slightly tinted)
    bg_col = tuple(max(0, min(255, int(c * 0.18) + int(BG[i] * 0.82))) for i, c in enumerate(colour))
    rounded_rect(draw, [pill_x, pill_y, pill_x + pw, pill_y + ph],
                 radius=18, fill=bg_col + (255,),
                 outline=colour + (180,), width=1)
    draw.text((pill_x + pad_x, pill_y + pad_y), label, font=f_badge, fill=colour)

    pill_x += pw + gap
    if pill_x > W - 200:   # wrap to second row
        pill_x = x_start
        pill_y += ph + 10

# ── stat chips ────────────────────────────────────────────────────────────────
stats = [("45+", "Free Tools"), ("0", "Uploads Required"), ("100%", "Private")]
sx = x_start
sy = pill_y + 58

for val, lbl in stats:
    draw.text((sx, sy),       val, font=font(36, bold=True), fill=ACCENT)
    vw = int(draw.textlength(val, font=font(36, bold=True)))
    draw.text((sx + vw + 8, sy + 8), lbl,  font=f_label,    fill=TEXT_MUT)
    sx += vw + 8 + int(draw.textlength(lbl, font=f_label)) + 48

# ── bottom URL bar ────────────────────────────────────────────────────────────
bar_y = H - 58
draw.rectangle([0, bar_y, W, H], fill=tuple(max(0, c - 3) for c in BG))
# gradient line above bar
for i in range(W):
    frac = i / W
    c = tuple(int(ACCENT[ch] + (ACCENT_CYAN[ch] - ACCENT[ch]) * frac) for ch in range(3))
    draw.rectangle([i, bar_y, i+1, bar_y+1], fill=c)

url = "tools.lumaplayground.com"
uw  = int(draw.textlength(url, font=f_url))
draw.text(((W - uw) // 2, bar_y + 14), url, font=f_url, fill=TEXT_MUT)

# ── save ──────────────────────────────────────────────────────────────────────
img.save(OUT, 'PNG', optimize=True)
print(f"Saved {OUT}  ({W}x{H})")
