"""Convert MAIN/gfx/2d/hudchars.tga into MAIN/fonts/hudchars.ttf.

Usage: python convert.py <path to MAIN/gfx/2d> <path to MAIN/fonts>

hudchars_OSP1.tga/OSP2.tga are intentionally not converted -- they load as
plain bitmap textures again, same as before this project started. Their
two-tone fill/outline needed a vector erosion step (insetting the outline
silhouette by a constant stroke width) that depended on polygon boolean
math incompatible with the curved segments contours.py now produces, and
adapting that pipeline for curves was judged not worth it against just
reverting those two fonts to bitmap rendering.
"""
import os
import sys

from tga_reader import read_tga, slice_grid
from masks import alpha_grid
from contours import trace_field, pixel_loops_to_font_units
from alignment import align_glyph_set
from metrics import proportional_metrics
from build_ttf import build_font

SOURCE_CELL_PX = 16
UNITS_PER_EM = 1024
GRID_SIZE = 16
NUM_CELLS = GRID_SIZE * GRID_SIZE


def convert_single_layer(tga_path, ttf_path, family_name):
    image = read_tga(tga_path)
    cells = slice_grid(image, grid_size=GRID_SIZE)

    glyph_order = ['.notdef'] + [f'cell{i:03d}' for i in range(NUM_CELLS)]
    named_loops = {f'cell{i:03d}': trace_field(alpha_grid(cell)) for i, cell in enumerate(cells)}
    named_loops = align_glyph_set(named_loops)

    glyph_contours = {name: pixel_loops_to_font_units(loops, SOURCE_CELL_PX, UNITS_PER_EM)
                       for name, loops in named_loops.items()}

    # proportional_metrics operates on already font-unit-scaled contours (not
    # the pixel-grid-space loops above) since its advance-width/side-bearing
    # values are font-unit quantities -- computing them any earlier would mean
    # comparing pixel-grid coordinates (0-16) against a font-unit side bearing
    metrics = proportional_metrics(glyph_contours, UNITS_PER_EM)
    glyph_contours = {name: shifted for name, (shifted, _, _) in metrics.items()}
    glyph_metrics = {name: (advance, lsb) for name, (_, advance, lsb) in metrics.items()}

    # every cell index is literally its own Unicode codepoint -- ASCII
    # 32-126 directly, and RTCW's extended range is genuinely Windows-1252/
    # Latin-1-compatible (e.g. cell 0xC0 is "A-grave", matching Latin-1
    # exactly). This must match how the engine looks glyphs up
    # (stbtt_FindGlyphIndex(font, cellIndex), see tr_font_bake.c) --
    # cell indices with no real Unicode meaning (icon/dingbat cells in the
    # control-code range) still get a cmap entry here so *our own* font
    # keeps working, even though an arbitrary external font obviously won't
    # have anything meaningful mapped there.
    cmap = {i: f'cell{i:03d}' for i in range(NUM_CELLS)}

    build_font(glyph_order, glyph_contours, glyph_metrics, UNITS_PER_EM, cmap, ttf_path, family_name)


VARIANTS = [
    ('hudchars.tga', 'hudchars.ttf', 'hudchars', convert_single_layer),
]


def main():
    if len(sys.argv) != 3:
        print("usage: convert.py <MAIN/gfx/2d dir> <output MAIN/fonts dir>")
        sys.exit(1)
    src_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    for tga_name, ttf_name, family_name, convert_fn in VARIANTS:
        tga_path = os.path.join(src_dir, tga_name)
        ttf_path = os.path.join(out_dir, ttf_name)
        print(f"converting {tga_path} -> {ttf_path}")
        convert_fn(tga_path, ttf_path, family_name)


if __name__ == '__main__':
    main()
