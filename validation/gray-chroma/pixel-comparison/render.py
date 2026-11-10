"""Generate a deterministic RGB test card, then display real FFmpeg output."""
from pathlib import Path
import colorsys
import hashlib
import json
import sys
from PIL import Image, ImageDraw, ImageFont, ImageChops

ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 512, 320

if sys.argv[1] == "input":
    image = Image.new("RGB", (WIDTH, HEIGHT))
    pixels = image.load()
    swatches = [(255, 255, 255), (255, 255, 0), (0, 255, 255),
                (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if y < 112:
                color = swatches[x * len(swatches) // WIDTH]
            elif y < 216:
                color = tuple(round(v * 255) for v in colorsys.hsv_to_rgb(
                    x / WIDTH, 1, 0.2 + 0.8 * (y - 112) / 103))
            else:
                val = round(x * 255 / (WIDTH - 1))
                color = (val, val, val)
            pixels[x, y] = color
    draw = ImageDraw.Draw(image)
    for x, color in [(72, "#e95252"), (184, "#48ce83"),
                     (296, "#4f8af5"), (408, "#ffd154")]:
        draw.ellipse((x - 34, 232, x + 34, 300), fill=color,
                     outline=(255, 255, 255), width=3)
    image.save(ROOT / "source.png")
    # RGB0 is R,G,B plus an ignored padding byte; padding varies deliberately.
    rgb = image.tobytes()
    raw = bytearray(WIDTH * HEIGHT * 4)
    for i in range(WIDTH * HEIGHT):
        raw[4 * i:4 * i + 3] = rgb[3 * i:3 * i + 3]
        raw[4 * i + 3] = (i * 73 + 19) & 255
    (ROOT / "source.rgb0").write_bytes(raw)
    sys.exit(0)

baseline = Image.open(ROOT / "unchanged.pgm").convert("L")
patched = Image.open(ROOT / "patched.pgm").convert("L")
delta = ImageChops.difference(baseline, patched)
hist = delta.histogram()
stats = {
    "conversion": "RGB0 -> GRAY8, 512x320, BT.709, full range, bilinear",
    "pixels_compared": WIDTH * HEIGHT,
    "differing_pixels": sum(hist[1:]),
    "maximum_absolute_pixel_difference": max(i for i, count in enumerate(hist) if count),
    "unchanged_sha256_active_pixels": hashlib.sha256(baseline.tobytes()).hexdigest(),
    "patched_sha256_active_pixels": hashlib.sha256(patched.tobytes()).hexdigest(),
    "unchanged": json.loads((ROOT / "unchanged.json").read_text()),
    "patched": json.loads((ROOT / "patched.json").read_text()),
}
assert stats["differing_pixels"] == 0, stats
assert stats["unchanged"]["uv_converter_calls"] > 0, stats
assert stats["patched"]["uv_converter_calls"] == 0, stats
(ROOT / "results.json").write_text(json.dumps(stats, indent=2) + "\n")
baseline.save(ROOT / "unchanged.png")
patched.save(ROOT / "patched.png")
delta.save(ROOT / "difference.png")

font_root = Path("/usr/share/fonts/TTF")
def font(size, bold=False):
    return ImageFont.truetype(str(font_root / (
        "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")), size)

canvas = Image.new("RGB", (1632, 548), "#121923")
draw = ImageDraw.Draw(canvas)
draw.text((24, 18), "Same image. Same grayscale pixels. Less work.",
          font=font(30, True), fill="#f3f6fa")
draw.text((24, 63), "Real output from separately linked unchanged and patched FFmpeg builds",
          font=font(21), fill="#acbbcd")
panels = [
    (Image.open(ROOT / "source.png"), "Input: RGB0", "512 x 320 color test pattern"),
    (baseline.convert("RGB"), "Unchanged FFmpeg", "U/V converter calls: %d" % stats["unchanged"]["uv_converter_calls"]),
    (patched.convert("RGB"), "Our patch", "U/V converter calls: 0"),
]
for i, (image, title, caption) in enumerate(panels):
    x = 24 + i * 536
    draw.text((x, 108), title, font=font(25, True), fill="#f3f6fa")
    canvas.paste(image, (x, 151))
    draw.text((x, 485), caption, font=font(21), fill="#acbbcd")
draw.text((24, 518), "Output comparison: 0 differing pixels out of 163,840 | Maximum difference: 0 / 255",
          font=font(18), fill="#73e0b2")
canvas.save(ROOT / "comparison.png")
print(json.dumps(stats, indent=2))
