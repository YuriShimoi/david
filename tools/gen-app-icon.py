# Builds the program icon from assets/icons/david.svg (the official colored icon): david.ico (16-256 px, for the
# exe and the installer) and david-256.png (Linux window and AppImage). The SVG is one polygon path filled with a
# linear gradient; both are read from the file and drawn 4x supersampled.
# Usage: python tools/gen-app-icon.py
import os, re
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, 'assets', 'icons')
svg = open(os.path.join(ICONS, 'david.svg'), encoding='utf-8').read()

# The polygon: absolute M / L commands
d = re.search(r'<path d="([^"]+)"', svg).group(1)
points = [(float(x), float(y)) for x, y in re.findall(r'[ML]\s*([-\d.]+)\s+([-\d.]+)', d)]

# The gradient: bounding-box coordinates through gradientTransform, and its stops
g = re.search(r'<linearGradient[^>]*x1="([-\d.]+)" y1="([-\d.]+)" x2="([-\d.]+)" y2="([-\d.]+)" gradientTransform="matrix\(([^)]+)\)"', svg)
x1, y1, x2, y2 = map(float, g.groups()[:4])
a, b, c, dd, e, f = map(float, g.group(5).split(','))
p1 = np.array([a * x1 + c * y1 + e, b * x1 + dd * y1 + f])
p2 = np.array([a * x2 + c * y2 + e, b * x2 + dd * y2 + f])
stops = [(float(o) / 100, tuple(int(v) for v in rgb.split(',')))
         for o, rgb in re.findall(r'<stop offset="([\d.]+)%"[^>]*stop-color:rgb\(([\d, ]+)\)', svg)]

def render(size, ss=4):
  n = size * ss
  k = n / 1000
  mask = Image.new('L', (n, n), 0)
  ImageDraw.Draw(mask).polygon([(x * k, y * k) for x, y in points], fill=255)
  # Gradient: project each pixel center (in SVG units) onto p1 -> p2
  ys, xs = np.mgrid[0:n, 0:n]
  px, py = (xs + 0.5) / k, (ys + 0.5) / k
  v = p2 - p1
  t = np.clip(((px - p1[0]) * v[0] + (py - p1[1]) * v[1]) / (v @ v), 0, 1)
  rgb = np.zeros((n, n, 3))
  for ch in range(3):
    rgb[..., ch] = np.interp(t, [s[0] for s in stops], [s[1][ch] for s in stops])
  img = Image.fromarray(rgb.astype(np.uint8)).convert('RGBA')
  img.putalpha(mask)
  return img.resize((size, size), Image.LANCZOS)

sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = {s: render(s) for s in sizes}
imgs[256].save(os.path.join(ICONS, 'david-256.png'))
imgs[256].save(os.path.join(ICONS, 'david.ico'), sizes=[(s, s) for s in sizes], append_images=[imgs[s] for s in sizes[:-1]])
print('ok', len(points), 'points,', len(stops), 'stops')
