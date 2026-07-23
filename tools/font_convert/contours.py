"""Trace glyph-cell fields into vector outlines using potrace, classifying
each traced segment as either a straight line (including diagonals) or a
smooth cubic Bezier curve. Replaced an earlier marching-squares +
Douglas-Peucker tracer: a single simplification tolerance can't tell "this
is a curve" from "this is a sharp corner", but potrace does per-vertex."""
import math

import numpy as np
import potrace
from scipy.ndimage import gaussian_filter
from skimage import transform

LEVEL = 128.0
# Blurs the native 16x16 grid before upsampling/tracing, removing sub-pixel
# antialiasing noise instead of compensating for it downstream via alphamax.
# NOTE: bound into trace_field's default at def-time -- reassigning this
# afterward does NOT affect it; pass smooth_sigma= explicitly to override.
GRID_SMOOTH_SIGMA = 0.5
# potrace's own default; combined with high supersampling this classifies
# real corners vs. curves per-vertex instead of faceting round counters.
SUPERSAMPLE = 32
ALPHAMAX = 1.0
# Raised from potrace's default (0.2), which under-fits this font's small
# "lowercase" letterforms; kept below the point where corners start rounding.
OPTTOLERANCE = 0.3
TURDSIZE = 2
AXIS_SNAP_MAX_ANGLE_DEG = 4.0
COLLINEAR_MERGE_TOLERANCE = 0.15


def trace_field(value_grid, level=LEVEL, supersample=SUPERSAMPLE,
                 alphamax=ALPHAMAX, opttolerance=OPTTOLERANCE, turdsize=TURDSIZE,
                 smooth_sigma=GRID_SMOOTH_SIGMA):
    """value_grid: list of rows of float values (see masks.py's alpha_grid).
    Returns a list of closed loops; each loop is (start_point, segments)
    where start_point is an (x, y) tuple and segments is a list of tagged
    entries:
      ('line', (x, y))                          -- straight line to (x,y)
      ('curve', (c1x,c1y), (c2x,c2y), (x, y))    -- cubic Bezier to (x,y)
    The last segment's endpoint always equals start_point exactly (potrace
    produces genuinely closed paths).

    Coordinates are in the same (x-right, y-down) pixel-grid space as
    value_grid, sub-pixel-accurate since potrace traces against the
    bicubic-supersampled field rather than the native 16x16 grid -- the
    same reason the old tracer supersampled before running marching
    squares: a 90-degree pixel-art corner or a diagonal stroke's
    antialiasing staircase both need finer-than-source-pixel resolution
    to read as sharp/straight once magnified, not chamfered/wavy.

    potrace.Bitmap always inverts whatever boolean array it's given
    (its __init__ unconditionally calls self.invert()) -- pass the
    complement of "is ink" so that inversion cancels out and ink is
    foreground in potrace's sense too, rather than the whole cell tracing
    as one background rectangle with the glyph as a hole in it."""
    original = np.asarray(value_grid, dtype=float)
    if smooth_sigma > 0:
        original = gaussian_filter(original, sigma=smooth_sigma, mode='nearest')
    upsampled = transform.resize(original, (original.shape[0] * supersample, original.shape[1] * supersample),
                                  order=3, mode='edge', anti_aliasing=True, preserve_range=True)
    ink = upsampled >= level
    bitmap = potrace.Bitmap(~ink)
    path = bitmap.trace(turdsize=turdsize, alphamax=alphamax, opticurve=True, opttolerance=opttolerance)

    loops = []
    for curve in path.curves:
        start = (curve.start_point.x / supersample, curve.start_point.y / supersample)
        segments = []
        for seg in curve.segments:
            if seg.is_corner:
                # potrace represents a corner as two lines through its own
                # on-curve vertex `c`, not a Bezier control point
                segments.append(('line', (seg.c.x / supersample, seg.c.y / supersample)))
                segments.append(('line', (seg.end_point.x / supersample, seg.end_point.y / supersample)))
            else:
                segments.append(('curve',
                                  (seg.c1.x / supersample, seg.c1.y / supersample),
                                  (seg.c2.x / supersample, seg.c2.y / supersample),
                                  (seg.end_point.x / supersample, seg.end_point.y / supersample)))
        start, segments = _merge_collinear_lines(start, segments)
        loops.append(_snap_axis_aligned_lines(start, segments))
    return loops


def _merge_collinear_lines(start, segments, tolerance=COLLINEAR_MERGE_TOLERANCE):
    """potrace's own polygon-fitting step can leave multiple collinear
    vertices along a single straight run instead of one clean 2-point line
    -- its corner/curve decision doesn't separately try to minimize vertex
    count on a run it already considers "corners." Repeatedly drop any
    'line' vertex that sits within tolerance of the straight line between
    its neighbors, as long as both neighboring edges are also 'line'
    segments -- never touches a vertex next to a 'curve' segment, since
    removing it would change that curve's own shape, not just simplify a
    straight run."""
    segs = list(segments)
    changed = True
    while changed and len(segs) >= 3:
        changed = False
        n = len(segs)
        for i in range(n):
            if segs[i][0] != 'line':
                continue
            next_i = (i + 1) % n
            if segs[next_i][0] != 'line':
                continue
            prev_i = (i - 1) % n
            x0, y0 = segs[prev_i][-1]
            x1, y1 = segs[i][-1]
            x2, y2 = segs[next_i][-1]
            dx, dy = x2 - x0, y2 - y0
            length = math.hypot(dx, dy)
            if length == 0:
                continue
            dist = abs((x1 - x0) * dy - (y1 - y0) * dx) / length
            if dist <= tolerance:
                del segs[i]
                changed = True
                break
    return segs[-1][-1], segs


def _snap_axis_aligned_lines(start, segments, max_angle_deg=AXIS_SNAP_MAX_ANGLE_DEG):
    """A straight vertical or horizontal stroke in the source pixel art
    should trace as an exactly vertical/horizontal line, but real
    antialiasing noise can leave it a degree or so off axis -- invisible at
    16px, but a visible lean once magnified to a large rendered size. Snap
    any 'line' segment already within max_angle_deg of vertical or
    horizontal to be exactly that, by averaging its endpoints' shared
    coordinate. Curve segments are never touched -- a Bezier control point
    isn't a straight edge to snap, and doing so would distort the curve.

    Angle decisions and averages are computed from the original,
    unmodified segment endpoints so that snapping one edge can't shift the
    angle the next edge is judged against. Segments form a closed loop
    (the last one's endpoint is `start`), so indices wrap via Python's
    native negative-index behavior.

    A 'line' segment immediately following a 'curve' segment shares its
    start point with that curve's own endpoint -- snapping such an edge
    would silently move the curve's endpoint too, contradicting "curve
    segments are never touched" above. Skip snapping an edge unless the
    segment before it is also a 'line', so only a genuine all-line run
    ever gets its shared vertices moved."""
    n = len(segments)
    if n == 0:
        return start, segments
    end_points = [seg[-1] for seg in segments]
    new_x = [p[0] for p in end_points]
    new_y = [p[1] for p in end_points]
    for i, seg in enumerate(segments):
        if seg[0] != 'line' or segments[i - 1][0] != 'line':
            continue
        x0, y0 = end_points[i - 1]
        x1, y1 = end_points[i]
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy)
        if length == 0:
            continue
        angle_from_horizontal = math.degrees(math.atan2(abs(dy), abs(dx)))
        if angle_from_horizontal >= 90 - max_angle_deg:
            avg_x = (x0 + x1) / 2.0
            new_x[i - 1] = avg_x
            new_x[i] = avg_x
        elif angle_from_horizontal <= max_angle_deg:
            avg_y = (y0 + y1) / 2.0
            new_y[i - 1] = avg_y
            new_y[i] = avg_y

    new_segments = []
    for i, seg in enumerate(segments):
        if seg[0] == 'line':
            new_segments.append(('line', (new_x[i], new_y[i])))
        else:
            new_segments.append(('curve', seg[1], seg[2], (new_x[i], new_y[i])))
    return (new_x[-1], new_y[-1]), new_segments


def pixel_loops_to_font_units(loops, source_cell_px, units_per_em):
    """Convert (x, y) pixel-grid coordinates (y-down, possibly sub-pixel)
    to (x, y) TrueType font-unit integer coordinates (y-up), preserving
    each segment's line/curve tag. The (source_cell_px - y) flip exactly
    cancels the pixel-space/font-space axis-direction difference without
    needing to reverse point order or winding."""
    k = units_per_em / source_cell_px

    def convert(pt):
        x, y = pt
        return (int(round(x * k)), int(round((source_cell_px - y) * k)))

    out = []
    for start, segments in loops:
        new_segments = []
        for seg in segments:
            if seg[0] == 'line':
                new_segments.append(('line', convert(seg[1])))
            else:
                _, c1, c2, end = seg
                new_segments.append(('curve', convert(c1), convert(c2), convert(end)))
        out.append((convert(start), new_segments))
    return out
