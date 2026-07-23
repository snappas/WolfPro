"""Render a composited preview of hudchars.ttf text without needing the
game or Windows Font Viewer -- and, unlike Font Viewer, correctly flattens
the quadratic Bezier curves potrace/Cu2QuPen now produce (Font Viewer's own
rasterizer handles those fine too, but this tool exists so you don't need
to open Font Viewer at all to check a glyph after tweaking a parameter).

Usage: python preview.py <ttf path> <text> [output.png]
"""
import sys

import numpy as np
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen
from PIL import Image, ImageDraw

CELL_PX = 64  # matches FONT_BAKE_PX_PER_CELL in tr_font_bake.c
CURVE_FLATTEN_STEPS = 8


class _ContourCollectorPen(BasePen):
    """Collects each contour as a flat list of (x, y) points, flattening
    quadratic curves into short line segments -- good enough for a raster
    preview, not meant to be geometrically exact."""

    def __init__(self):
        super().__init__(None)
        self.contours = []
        self._current = []

    def _moveTo(self, pt):
        self._current = [pt]

    def _lineTo(self, pt):
        self._current.append(pt)

    def _qCurveToOne(self, control, pt):
        p0 = self._current[-1]
        for step in range(1, CURVE_FLATTEN_STEPS + 1):
            t = step / CURVE_FLATTEN_STEPS
            x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * control[0] + t ** 2 * pt[0]
            y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * control[1] + t ** 2 * pt[1]
            self._current.append((x, y))

    def _closePath(self):
        self.contours.append(self._current)
        self._current = []

    def _endPath(self):
        self.contours.append(self._current)
        self._current = []


def _glyph_contours(font, name):
    glyf = font['glyf']
    if name not in font.getGlyphOrder():
        return []
    g = glyf[name]
    if g.numberOfContours <= 0:
        return []
    pen = _ContourCollectorPen()
    g.draw(pen, glyf)
    return pen.contours


def _rasterize_xor(contours, upm, px):
    """XOR-composite each contour's own filled polygon -- correct for the
    single level of outer+hole nesting these glyphs actually have."""
    img = np.zeros((px, px), dtype=np.uint8)
    for contour in contours:
        layer = Image.new('L', (px, px), 0)
        draw = ImageDraw.Draw(layer)
        points = [(x / upm * px, (1 - y / upm) * px) for x, y in contour]
        draw.polygon(points, fill=255)
        img = np.bitwise_xor(img, np.array(layer))
    return img.astype(float)


def render_cell(font, cell_index, px=CELL_PX):
    upm = font['head'].unitsPerEm
    cov = _rasterize_xor(_glyph_contours(font, f'cell{cell_index:03d}'), upm, px)
    rgb = np.full_like(cov, 255.0)
    alpha = cov

    out = np.zeros((px, px, 4), dtype=np.uint8)
    out[:, :, 0] = rgb
    out[:, :, 1] = rgb
    out[:, :, 2] = rgb
    out[:, :, 3] = alpha
    return out


def render_text(ttf_path, text, px=CELL_PX, bg=(60, 60, 60)):
    font = TTFont(ttf_path)
    canvas = Image.new('RGB', (px * len(text), px), bg)
    for i, ch in enumerate(text):
        cell_index = ord(ch) if ord(ch) < 256 else 0
        cell_rgba = Image.fromarray(render_cell(font, cell_index, px), 'RGBA')
        canvas.paste(cell_rgba, (i * px, 0), cell_rgba)
    return canvas


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("usage: preview.py <ttf path> <text> [output.png]")
        sys.exit(1)
    out_path = sys.argv[3] if len(sys.argv) > 3 else 'preview.png'
    render_text(sys.argv[1], sys.argv[2]).save(out_path)
    print(f"saved {out_path}")
