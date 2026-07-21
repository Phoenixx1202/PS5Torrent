#!/usr/bin/env python3
"""
PS5Torrent Icon Generator v2.0
Generates icon0.png, pic0.png, pic1.png for PS5 PKG using Pillow.
Much faster than v1 - uses vectorized drawing operations.

Requires: pip install Pillow
"""

import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont, ImageFilter
except ImportError:
    print("❌ Pillow not installed!")
    print("   Run: pip3 install Pillow")
    print("   Or:  python3 -m pip install Pillow")
    sys.exit(1)


def create_rounded_rect(size, radius, bg_color):
    """Create a rounded rectangle image."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Draw rounded rectangle
    draw.rounded_rectangle(
        [(0, 0), (size - 1, size - 1)],
        radius=radius,
        fill=bg_color
    )

    return img


def generate_torrent_icon(size=512):
    """Generate a crisp home-screen tile with download and swarm cues."""
    scale = 2
    canvas = size * scale
    radius = size // 6 * scale
    img = Image.new("RGB", (canvas, canvas), (5, 10, 20))
    pixels = img.load()

    # Deep navy gradient plus two soft cyan/blue light sources.
    for y in range(canvas):
        for x in range(canvas):
            vertical = y / canvas
            blue_glow = max(0.0, 1.0 - (((x - canvas * .78) ** 2 +
                                         (y - canvas * .16) ** 2) ** .5) /
                            (canvas * .72))
            cyan_glow = max(0.0, 1.0 - (((x - canvas * .18) ** 2 +
                                         (y - canvas * .70) ** 2) ** .5) /
                            (canvas * .65))
            pixels[x, y] = (
                int(5 + 4 * vertical + 4 * cyan_glow),
                int(11 + 13 * vertical + 27 * cyan_glow + 10 * blue_glow),
                int(22 + 21 * vertical + 50 * blue_glow + 30 * cyan_glow),
            )

    # Rounded tile mask.
    mask = Image.new("L", (canvas, canvas), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, canvas - 1, canvas - 1), radius=radius, fill=255)
    img.putalpha(mask)
    draw = ImageDraw.Draw(img)
    unit = scale

    # Layered border gives the tile a polished edge on both light and dark UI.
    draw.rounded_rectangle((3*unit, 3*unit, canvas-4*unit, canvas-4*unit),
                           radius=radius-3*unit, outline=(70, 156, 255, 170),
                           width=2*unit)
    draw.rounded_rectangle((10*unit, 10*unit, canvas-11*unit, canvas-11*unit),
                           radius=radius-10*unit, outline=(70, 220, 222, 45),
                           width=unit)

    cx = canvas // 2
    cy = int(canvas * .39)

    # A peer-to-peer constellation surrounding the download glyph.
    nodes = [
        (int(canvas*.24), int(canvas*.30)),
        (int(canvas*.76), int(canvas*.30)),
        (int(canvas*.18), int(canvas*.53)),
        (int(canvas*.82), int(canvas*.53)),
        (int(canvas*.31), int(canvas*.64)),
        (int(canvas*.69), int(canvas*.64)),
    ]
    for x, y in nodes:
        draw.line((x, y, cx, cy), fill=(54, 132, 203, 100), width=2*unit)
    for index, (x, y) in enumerate(nodes):
        r = (6 if index < 2 else 5) * unit
        draw.ellipse((x-r, y-r, x+r, y+r),
                     fill=(77, 221, 218, 255), outline=(182, 255, 250, 220),
                     width=2*unit)

    # Dark medallion and bright, unmistakable download arrow.
    outer = 122 * unit
    draw.ellipse((cx-outer, cy-outer, cx+outer, cy+outer),
                 fill=(8, 25, 45, 238), outline=(53, 127, 211, 230),
                 width=3*unit)
    inner = 105 * unit
    draw.ellipse((cx-inner, cy-inner, cx+inner, cy+inner),
                 outline=(62, 220, 220, 90), width=2*unit)
    arrow = (78, 187, 255, 255)
    highlight = (105, 239, 225, 255)
    stem_w = 38 * unit
    top = cy - 70 * unit
    bottom = cy + 34 * unit
    draw.rounded_rectangle((cx-stem_w//2, top, cx+stem_w//2, bottom),
                           radius=10*unit, fill=arrow)
    draw.polygon(((cx-78*unit, cy+16*unit), (cx+78*unit, cy+16*unit),
                  (cx, cy+91*unit)), fill=arrow)
    draw.line((cx-8*unit, top+10*unit, cx-8*unit, bottom-4*unit),
              fill=highlight, width=6*unit)

    # Download progress indicator.
    bar_x = 96 * unit
    bar_y = int(canvas * .72)
    bar_w = canvas - 2 * bar_x
    bar_h = 15 * unit
    draw.rounded_rectangle((bar_x, bar_y, bar_x+bar_w, bar_y+bar_h),
                           radius=bar_h//2, fill=(8, 19, 34, 255),
                           outline=(44, 76, 111, 255), width=unit)
    draw.rounded_rectangle((bar_x+2*unit, bar_y+2*unit,
                            bar_x+int(bar_w*.71), bar_y+bar_h-2*unit),
                           radius=(bar_h-4*unit)//2, fill=(69, 213, 214, 255))

    # Compact title, legible from the PS5 home carousel.
    try:
        font_size = 31 * unit
        font_paths = [
            "/System/Library/Fonts/SFNS.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        ]
        font = None
        for fp in font_paths:
            try:
                font = ImageFont.truetype(fp, font_size)
                break
            except (IOError, OSError):
                continue
        if font is None:
            font = ImageFont.load_default()

        text = "PS5 TORRENT"
        bbox = draw.textbbox((0, 0), text, font=font)
        text_w = bbox[2] - bbox[0]
        text_x = (canvas - text_w) // 2
        text_y = int(canvas * .82)
        draw.text((text_x+2*unit, text_y+2*unit), text, font=font,
                  fill=(0, 0, 0, 160))
        draw.text((text_x, text_y), text, font=font,
                  fill=(232, 246, 255, 255))
    except Exception:
        pass

    return img.resize((size, size), Image.Resampling.LANCZOS)


def generate_background(width=1920, height=1080):
    """Generate the background shown when the home-screen tile is selected."""
    img = Image.new("RGB", (width, height), (5, 10, 19))
    pixels = img.load()
    for y in range(height):
        for x in range(width):
            glow = max(0.0, 1.0 - (((x - width*.22) ** 2 +
                                    (y - height*.42) ** 2) ** .5) /
                       (width*.62))
            edge = max(0.0, 1.0 - (((x - width*.94) ** 2 +
                                    (y - height*.08) ** 2) ** .5) /
                       (width*.50))
            pixels[x, y] = (int(5 + glow*2),
                            int(11 + glow*20 + edge*5),
                            int(22 + glow*44 + edge*30))
    draw = ImageDraw.Draw(img, "RGBA")

    # Quiet perspective grid and atmospheric bands.
    for x in range(-height, width + height, 120):
        draw.line((x, height, x + height//2, 0), fill=(72, 157, 255, 18), width=1)
    for y in range(80, height, 96):
        draw.line((0, y, width, y), fill=(72, 157, 255, 14), width=1)
    draw.ellipse((-300, 140, 1050, 1490), outline=(53, 148, 255, 35), width=70)
    draw.ellipse((-180, 260, 930, 1370), outline=(75, 228, 219, 28), width=3)

    # Large swarm/download emblem on the left.
    cx, cy = int(width*.25), int(height*.43)
    nodes = [(cx-250,cy-120),(cx+255,cy-135),(cx-300,cy+95),
             (cx+300,cy+100),(cx-180,cy+230),(cx+190,cy+235)]
    for x, y in nodes:
        draw.line((x, y, cx, cy), fill=(74, 170, 235, 75), width=3)
        draw.ellipse((x-10,y-10,x+10,y+10), fill=(78,225,218,220),
                     outline=(220,255,255,220), width=2)
    draw.ellipse((cx-190,cy-190,cx+190,cy+190), fill=(5,16,30,190),
                 outline=(73,166,255,200), width=5)
    draw.ellipse((cx-164,cy-164,cx+164,cy+164), outline=(80,228,220,120), width=3)
    draw.rounded_rectangle((cx-34,cy-118,cx+34,cy+36), radius=17,
                           fill=(80,184,255,255))
    draw.polygon(((cx-118,cy+8),(cx+118,cy+8),(cx,cy+130)),
                 fill=(80,184,255,255))

    # Branding and short product promise on the right.
    try:
        font_path = "/System/Library/Fonts/Helvetica.ttc"
        if not os.path.exists(font_path):
            font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
        title_font = ImageFont.truetype(font_path, 86)
        body_font = ImageFont.truetype(font_path, 31)
        small_font = ImageFont.truetype(font_path, 21)
        tx = int(width*.52)
        draw.text((tx, int(height*.33)), "PS5Torrent", font=title_font,
                  fill=(238,248,255,255), stroke_width=1,
                  stroke_fill=(23,64,105,255))
        draw.text((tx, int(height*.46)), "Downloads no console, controle na TV.",
                  font=body_font, fill=(155,190,222,255))
        draw.rounded_rectangle((tx, int(height*.56), tx+440, int(height*.615)),
                               radius=25, fill=(22,74,125,180),
                               outline=(69,170,245,150), width=2)
        draw.text((tx+24, int(height*.568)), "VELOCIDADE  •  PROGRESSO  •  ETA",
                  font=small_font, fill=(113,230,224,255))
    except Exception:
        pass

    return img


if __name__ == "__main__":
    out_dir = os.path.join(os.path.dirname(__file__), "sce_sys")
    os.makedirs(out_dir, exist_ok=True)

    print("🎮 PS5Torrent Icon Generator v2.0 (vectorized)")
    print(f"   Output: {out_dir}/")
    print()

    # Generate icon0.png (512x512 app icon)
    print("  Generating icon0.png (512x512)...")
    icon = generate_torrent_icon(512)
    icon_path = os.path.join(out_dir, "icon0.png")
    icon.save(icon_path, "PNG")
    fsize = os.path.getsize(icon_path)
    print(f"  ✅ icon0.png ({fsize:,} bytes)")

    # Generate pic0.png (1920x1080 background)
    print("  Generating pic0.png (1920x1080)...")
    bg = generate_background(1920, 1080)
    bg_path = os.path.join(out_dir, "pic0.png")
    bg.save(bg_path, "PNG")
    fsize = os.path.getsize(bg_path)
    print(f"  ✅ pic0.png ({fsize:,} bytes)")

    # Generate pic1.png (startup image, same as pic0 for simplicity)
    print("  Generating pic1.png (1920x1080)...")
    bg_path = os.path.join(out_dir, "pic1.png")
    bg.save(bg_path, "PNG")
    fsize = os.path.getsize(bg_path)
    print(f"  ✅ pic1.png ({fsize:,} bytes)")

    print()
    print("📁 Icons saved to:", out_dir)
    print("🎉 Done!")
