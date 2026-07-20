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
    """Generate a PS5Torrent app icon with torrent/download theme."""
    # Create base rounded rect
    bg = (13, 17, 23)  # Dark background
    img = create_rounded_rect(size, size // 6, bg + (255,))
    draw = ImageDraw.Draw(img)

    # Gradient overlay (darker at top)
    for y in range(size):
        alpha = int(30 * (1 - y / size))
        draw.rectangle([(0, y), (size, y + 1)], fill=(0, 0, 0, alpha))

    # Add a subtle blue glow at bottom
    for y in range(size):
        alpha = int(20 * (y / size))
        draw.rectangle([(0, y), (size, y + 1)], fill=(31, 111, 235, alpha))

    # Border glow
    glow_color = (58, 166, 255, 40)
    draw.rounded_rectangle(
        [(2, 2), (size - 3, size - 3)],
        radius=size // 6 - 2,
        outline=glow_color,
        width=2
    )

    # === Draw download arrow ===
    cx, cy = size // 2, size // 2 - size // 10

    # Arrow stem (vertical rectangle)
    stem_w = size // 6
    stem_h = size // 4
    arrow_color = (88, 166, 255)  # Blue
    highlight = (150, 200, 255)

    # Stem (vertical bar)
    stem_x1 = cx - stem_w // 2
    stem_y1 = cy - stem_h // 2
    stem_x2 = cx + stem_w // 2
    stem_y2 = cy + stem_h // 2
    draw.rectangle([(stem_x1, stem_y1), (stem_x2, stem_y2)], fill=arrow_color + (255,))

    # Stem highlight
    hl_x = stem_x1 + stem_w // 6
    draw.rectangle(
        [(hl_x, stem_y1 + 5), (hl_x + stem_w // 8, stem_y2 - 5)],
        fill=highlight + (180,))

    # Arrow head (triangle pointing down)
    head_w = size // 2
    head_h = size // 3
    head_x1 = cx - head_w // 2
    head_x2 = cx + head_w // 2
    head_y1 = cy + stem_h // 2 - 5
    head_y2 = head_y1 + head_h

    # Draw filled triangle for arrow head
    for y in range(head_y1, head_y2):
        progress = (y - head_y1) / (head_y2 - head_y1)
        half_w = int(head_w // 2 * progress)
        x1 = cx - half_w
        x2 = cx + half_w
        if x1 < x2:
            draw.rectangle([(x1, y), (x2, y + 1)], fill=arrow_color + (255,))

    # === Draw progress bar ===
    bar_y = int(size * 0.78)
    bar_h = size // 20
    bar_margin = size // 5
    bar_width = size - 2 * bar_margin

    # Bar background
    draw.rectangle(
        [(bar_margin, bar_y), (bar_margin + bar_width, bar_y + bar_h)],
        fill=(30, 35, 45, 255))

    # Bar border
    draw.rectangle(
        [(bar_margin, bar_y), (bar_margin + bar_width, bar_y + bar_h)],
        outline=(58, 166, 255, 120), width=1)

    # Bar fill (60% for aesthetic appeal)
    fill_width = int(bar_width * 0.6)
    draw.rectangle(
        [(bar_margin + 1, bar_y + 1),
         (bar_margin + fill_width - 1, bar_y + bar_h - 1)],
        fill=(31, 111, 235, 255))

    # Bar fill gradient highlight
    draw.rectangle(
        [(bar_margin + 1, bar_y + 1),
         (bar_margin + fill_width - 1, bar_y + bar_h // 2)],
        fill=(58, 166, 255, 100))

    # === Draw network dots (decorative) ===
    dot_positions = [(0.25, 0.55), (0.4, 0.5), (0.6, 0.5), (0.75, 0.55)]
    for rx, ry in dot_positions:
        dx, dy = int(size * rx), int(size * ry)
        draw.ellipse([(dx - 3, dy - 3), (dx + 3, dy + 3)],
                     fill=(58, 166, 255, 80))

    # === Draw "PS5" text ===
    try:
        # Try to load a font
        font_size = size // 12
        # Try common font locations across macOS, Linux, and FreeBSD
        font_paths = [
            "/System/Library/Fonts/Helvetica.ttc",          # macOS
            "/System/Library/Fonts/Supplemental/Arial.ttf", # macOS
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",  # Linux
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",      # Linux
            "/usr/local/share/fonts/dejavu/DejaVuSans.ttf", # FreeBSD
            "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",    # Linux alt
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", # Linux alt
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

        text = "PS5Torrent"
        text_color = (200, 210, 220)
        text_y = int(size * 0.88)

        # Get text size
        bbox = draw.textbbox((0, 0), text, font=font)
        text_w = bbox[2] - bbox[0]
        text_x = (size - text_w) // 2

        # Text shadow
        shadow_color = (0, 0, 0, 150)
        draw.text((text_x + 1, text_y + 1), text, font=font,
                  fill=shadow_color)
        draw.text((text_x, text_y), text, font=font, fill=text_color + (255,))
    except Exception:
        pass  # Text is optional

    return img


def generate_background(width=1920, height=1080):
    """Generate pic0.png (background/hero image)."""
    img = Image.new("RGBA", (width, height), (13, 17, 23))
    draw = ImageDraw.Draw(img)

    # Gradient dark blue overlay
    for y in range(height):
        alpha = int(40 * (1 - y / height))
        draw.rectangle([(0, y), (width, y + 1)], fill=(31, 111, 235, alpha))

    # Grid lines (subtle)
    grid_color = (58, 166, 255, 20)
    for x in range(0, width, 80):
        draw.line([(x, 0), (x, height)], fill=grid_color, width=1)
    for y in range(0, height, 80):
        draw.line([(0, y), (width, y)], fill=grid_color, width=1)

    # Center glow
    cx, cy = width // 2, height // 2
    for r in range(200, 0, -10):
        alpha = int(15 * (1 - r / 200))
        draw.ellipse(
            [(cx - r, cy - r), (cx + r, cy + r)],
            fill=(58, 166, 255, alpha))

    # Decorative download arrow (large, subtle)
    arrow_color_s = (58, 166, 255, 30)
    ax, ay = cx, int(cy * 0.4)

    stem_w = width // 20
    stem_h = height // 8
    draw.rectangle(
        [(ax - stem_w // 2, ay - stem_h // 2),
         (ax + stem_w // 2, ay + stem_h // 2)],
        fill=arrow_color_s)

    head_h = height // 6
    for y in range(int(ay + stem_h // 2), int(ay + stem_h // 2 + head_h)):
        progress = (y - (ay + stem_h // 2)) / head_h
        half_w = int(width // 10 * progress)
        draw.rectangle(
            [(ax - half_w, y), (ax + half_w, y + 1)],
            fill=arrow_color_s)

    # Text
    try:
        font_size = 40
        try:
            font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", font_size)
        except (IOError, OSError):
            font = ImageFont.load_default()

        text = "PS5Torrent"
        bbox = draw.textbbox((0, 0), text, font=font)
        text_w = bbox[2] - bbox[0]
        draw.text(((width - text_w) // 2, int(height * 0.7)), text,
                  font=font, fill=(200, 210, 220, 255))

        subtext = "Download Manager for PlayStation 5"
        font_size = 24
        try:
            font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", font_size)
        except (IOError, OSError):
            pass
        bbox2 = draw.textbbox((0, 0), subtext, font=font)
        st_w = bbox2[2] - bbox2[0]
        draw.text(((width - st_w) // 2, int(height * 0.75)), subtext,
                  font=font, fill=(140, 150, 160, 200))
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
