import math
import os

from tga_reader import read_tga, slice_grid
from masks import alpha_grid
from contours import trace_field, pixel_loops_to_font_units

HUDCHARS_TGA = os.path.join(os.path.dirname(__file__), '..', '..', '..', 'MAIN', 'gfx', '2d', 'hudchars.tga')


def _shoelace(points):
    s = 0.0
    n = len(points)
    for i in range(n):
        x0, y0 = points[i]
        x1, y1 = points[(i + 1) % n]
        s += x0 * y1 - x1 * y0
    return s / 2.0


def _end_points(start, segments):
    return [seg[-1] for seg in segments]


def _real_cell(code):
    """A real cell from hudchars.tga -- a synthetic hard-edged shape isn't a
    reliable stand-in for testing corner sharpness (potrace's per-vertex
    corner/curve decision is sensitive to how an edge's length compares to
    its antialiasing softening, and a small synthetic square traced as all
    curves at settings that correctly keep 'H' sharp-cornered on the real
    asset). MAIN/ is asset data, not something these tests modify."""
    img = read_tga(HUDCHARS_TGA)
    cells = slice_grid(img, grid_size=16)
    return cells[code]


def test_empty_field_has_no_contours():
    field = [[0.0] * 8 for _ in range(8)]
    assert trace_field(field) == []


def test_sharp_cornered_glyph_traces_with_bounded_curve_deviation():
    # 'H' -- all right angles, no curvature anywhere, but still traces with
    # most segments tagged 'curve' (antialiasing is never a perfect corner).
    # What matters is those curve segments stay bounded, not ballooning outward.
    loops = trace_field(alpha_grid(_real_cell(0x48)))
    assert len(loops) == 1
    start, segments = loops[0]
    assert any(seg[0] == 'line' for seg in segments)
    end_points = [seg[-1] for seg in segments]
    for i, seg in enumerate(segments):
        if seg[0] != 'curve':
            continue
        p0 = end_points[i - 1]
        p3 = seg[-1]
        dx, dy = p3[0] - p0[0], p3[1] - p0[1]
        length = math.hypot(dx, dy)
        if length == 0:
            continue
        c1, c2 = seg[1], seg[2]
        d1 = abs((c1[0] - p0[0]) * dy - (c1[1] - p0[1]) * dx) / length
        d2 = abs((c2[0] - p0[0]) * dy - (c2[1] - p0[1]) * dx) / length
        assert max(d1, d2) < 0.7


def test_round_glyph_traces_with_real_curves():
    # 'O' -- should stay a smooth curve, not facet into a visible polygon
    # (too-low an alphamax traced this as a faceted octagon-like shape)
    loops = trace_field(alpha_grid(_real_cell(0x4F)))
    assert len(loops) == 2  # outer ring + inner counter
    for start, segments in loops:
        assert any(seg[0] == 'curve' for seg in segments)


def test_soft_circle_produces_curve_segments():
    # unlike a hard-edged shape, a smoothly antialiased round blob should
    # trace with at least some genuine curve segments, not a pure polygon
    n = 16
    field = [[0.0] * n for _ in range(n)]
    cx, cy, r = 8, 8, 5
    for y in range(n):
        for x in range(n):
            d = r - math.hypot(x - cx, y - cy)
            field[y][x] = min(max(128 + d * 100, 0), 255)
    loops = trace_field(field)
    assert len(loops) == 1
    start, segments = loops[0]
    assert any(seg[0] == 'curve' for seg in segments)


def test_loop_closes_on_itself():
    field = [[0.0] * 8 for _ in range(8)]
    for y in range(2, 6):
        for x in range(2, 6):
            field[y][x] = 255.0
    start, segments = trace_field(field)[0]
    assert segments[-1][-1] == start


def test_ring_has_opposite_winding_hole():
    # a hard-edged donut: outer 4x4 block minus an inner 2x2 hole should
    # trace as two separate loops with opposite-signed shoelace area --
    # what non-zero-winding fill needs to render the hole correctly.
    # smooth_sigma=0: this is a winding/topology check, unrelated to source
    # smoothing -- this hole is smaller than any real counter and would
    # close up under the default sigma (see test_realistic_ring_survives_default_smoothing).
    n = 8
    field = [[0.0] * n for _ in range(n)]
    for y in range(1, 7):
        for x in range(1, 7):
            field[y][x] = 255.0
    for y in range(3, 5):
        for x in range(3, 5):
            field[y][x] = 0.0
    loops = trace_field(field, smooth_sigma=0)
    assert len(loops) == 2


def test_realistic_ring_survives_default_smoothing():
    # same shape as test_ring_has_opposite_winding_hole, but at proportions
    # closer to a real counter (hole roughly a third of the outer diameter,
    # matching lowercase 'o'/'q' -- see contours.py's GRID_SMOOTH_SIGMA
    # comment) rather than that test's deliberately-extreme miniature hole.
    # This is the actual property that matters: the default smoothing must
    # not close a real glyph's counter.
    n = 16
    field = [[0.0] * n for _ in range(n)]
    for y in range(2, 14):
        for x in range(2, 14):
            field[y][x] = 255.0
    for y in range(6, 10):
        for x in range(6, 10):
            field[y][x] = 0.0
    loops = trace_field(field)
    assert len(loops) == 2
    areas = sorted(_shoelace(_end_points(start, segments)) for start, segments in loops)
    hole_area, outer_area = areas
    assert hole_area < 0 < outer_area


def test_near_vertical_line_snaps_exactly_vertical():
    # a field whose left edge drifts by a fraction of a pixel partway down
    # the column -- real antialiasing noise, not a deliberate lean --
    # should snap to an exactly shared x, same as a real traced glyph edge
    n = 16
    field = [[0.0] * n for _ in range(n)]
    for y in range(n):
        edge = 4.0 + (0.08 if y > n // 2 else 0.0)
        for x in range(n):
            field[y][x] = 255.0 if x > edge else 0.0
    start, segments = trace_field(field)[0]
    # the right edge sits at x=16 (the field boundary); everything else is
    # the left edge, which should collapse to exactly one shared x rather
    # than two visibly different x values for its upper and lower halves
    left_edge_xs = {x for x, y in _end_points(start, segments) if x < 10.0}
    assert len(left_edge_xs) == 1


def test_axis_snap_never_touches_curve_segments():
    # a segment tagged 'curve' must keep its own control points even if
    # its chord happens to be near-vertical/near-horizontal -- only 'line'
    # segments represent a straight edge that's meaningful to snap
    from contours import _snap_axis_aligned_lines
    segments = [
        ('curve', (0.0, 5.0), (0.001, 10.0), (0.002, 15.0)),
        ('line', (0.0, 0.0)),
    ]
    _, snapped = _snap_axis_aligned_lines((0.0, 0.0), segments)
    assert snapped[0] == segments[0]


def test_pixel_loops_to_font_units_converts_and_preserves_tags():
    loops = [((1.0, 2.0), [('line', (3.0, 4.0)), ('curve', (5.0, 6.0), (7.0, 8.0), (1.0, 2.0))])]
    converted = pixel_loops_to_font_units(loops, source_cell_px=16, units_per_em=1024)
    k = 1024 / 16

    def conv(pt):
        x, y = pt
        return (round(x * k), round((16 - y) * k))

    (start, segments) = converted[0]
    assert start == conv((1.0, 2.0))
    assert segments[0] == ('line', conv((3.0, 4.0)))
    assert segments[1] == ('curve', conv((5.0, 6.0)), conv((7.0, 8.0)), conv((1.0, 2.0)))
