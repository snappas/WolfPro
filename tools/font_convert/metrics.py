"""Compute a real per-glyph advance width from each glyph's own traced ink
extent, instead of the uniform full-em-cell width the source 16x16 grid
baking implies. Every glyph sharing the same advance width (matching the
source grid, one fixed-size cell per glyph) makes the font behave like a
monospace font when used as a standalone font outside the game (e.g.
Windows Font Viewer) -- a narrow glyph like 'i' only fills a small fraction
of its cell, leaving a large gap before the next character, which reads as
"'i' has a built-in space."

This must NOT move the glyph's own ink: the runtime bake in tr_font_bake.c
rasterizes this same outline into a fixed-size cell spanning the *whole*
0..units_per_em em square, regardless of advance width -- it has no notion
of per-glyph advance at all, so shifting the ink would just relocate the
blank space inside that cell instead of removing it, and be far more
visible in-game than in a word processor. Only the exported metrics change
here; every glyph's outline renders exactly where it always did."""

DEFAULT_RIGHT_BEARING = 64


def _glyph_x_extent(loops):
    xs = []
    for start, segments in loops:
        xs.append(start[0])
        for seg in segments:
            if seg[0] == 'line':
                xs.append(seg[1][0])
            else:
                _, c1, c2, end = seg
                xs.append(c1[0])
                xs.append(c2[0])
                xs.append(end[0])
    if not xs:
        return None
    return min(xs), max(xs)


def proportional_metrics(glyph_contours, units_per_em, right_bearing=DEFAULT_RIGHT_BEARING, space_width=None):
    """glyph_contours: dict of name -> loops, already in font-unit space
    (contours.pixel_loops_to_font_units's output). Returns dict of name ->
    (loops, advance_width, left_side_bearing) -- loops are always the exact
    same object/coordinates passed in, never moved.

    left_side_bearing is the glyph's own natural ink_min_x (matching where
    its ink already sits -- required for glyf/hmtx consistency, not a
    stylistic choice), and advance_width is ink_max_x + right_bearing, so a
    standalone renderer's pen only travels as far as this glyph's own ink
    plus a fixed right margin instead of the full em. A glyph with no ink at
    all (space, and any other blank cell) has no extent to derive a width
    from -- it gets a fixed, normal-looking proportional space width
    instead."""
    if space_width is None:
        space_width = units_per_em // 3

    result = {}
    for name, loops in glyph_contours.items():
        extent = _glyph_x_extent(loops)
        if extent is None:
            result[name] = (loops, space_width, 0)
            continue

        ink_min_x, ink_max_x = extent
        advance_width = int(round(ink_max_x + right_bearing))
        result[name] = (loops, advance_width, ink_min_x)

    return result
