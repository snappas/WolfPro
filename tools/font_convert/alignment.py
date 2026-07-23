"""Snap straight-line coordinates that are already close to shared across
the WHOLE glyph set onto one canonical value each, rather than every glyph
only being internally consistent with itself (contours.py's
_snap_axis_aligned_lines operates per-glyph). This is what gives a
consistent baseline, cap-height, and stroke width across letters instead
of each one landing a fraction of a pixel apart purely from independent
antialiasing noise -- the same real-antialiasing-noise problem
_snap_axis_aligned_lines addresses within one glyph, extended across all
of them.

Runs on pixel-grid-space loops (contours.trace_field's output), before
pixel_loops_to_font_units -- never touches curve control points, only the
endpoints of already-exactly-axis-aligned 'line' segments (produced by
_snap_axis_aligned_lines), so it only ever unifies genuine straight
strokes, never distorts a curve.

This does not attempt to guarantee stylistic consistency of serifs, ears,
or terminals -- those are design-level properties of the source pixel art
itself, not something a coordinate-snapping pass can infer or enforce.
Spot-check those visually rather than assuming this pass covers them."""

CLUSTER_BUCKET = 0.15
MIN_CLUSTER_MEMBERS = 8
SNAP_DISTANCE = 0.3


def _axis_aligned_edges(loops):
    """Yield (axis, value) for every already-exactly-axis-aligned 'line'
    edge: axis 'y' for a horizontal edge (shared y), axis 'x' for a
    vertical edge (shared x). Diagonal lines and curves never qualify."""
    for start, segments in loops:
        end_points = [seg[-1] for seg in segments]
        for i, seg in enumerate(segments):
            if seg[0] != 'line':
                continue
            x0, y0 = end_points[i - 1]
            x1, y1 = end_points[i]
            if y0 == y1:
                yield 'y', y0
            elif x0 == x1:
                yield 'x', x0


def _cluster(values, bucket=CLUSTER_BUCKET, min_members=MIN_CLUSTER_MEMBERS):
    buckets = {}
    for v in values:
        buckets.setdefault(round(v / bucket), []).append(v)
    return [sum(members) / len(members) for members in buckets.values() if len(members) >= min_members]


def _nearest(value, clusters, max_distance):
    best, best_dist = None, max_distance
    for c in clusters:
        d = abs(value - c)
        if d <= best_dist:
            best, best_dist = c, d
    return best


def align_glyph_set(named_loops, snap_distance=SNAP_DISTANCE):
    """named_loops: dict of glyph name -> loops (contours.trace_field's
    output, in pixel-grid space). Returns a new dict with the same shape,
    each glyph's near-a-cluster axis-aligned line endpoints snapped to
    that cluster's canonical value."""
    y_values, x_values = [], []
    for loops in named_loops.values():
        for axis, value in _axis_aligned_edges(loops):
            (y_values if axis == 'y' else x_values).append(value)
    y_clusters = _cluster(y_values)
    x_clusters = _cluster(x_values)

    out = {}
    for name, loops in named_loops.items():
        out[name] = [_snap_loop(start, segments, x_clusters, y_clusters, snap_distance)
                      for start, segments in loops]
    return out


def _snap_loop(start, segments, x_clusters, y_clusters, snap_distance):
    n = len(segments)
    if n == 0:
        return start, segments
    end_points = [seg[-1] for seg in segments]
    new_pts = list(end_points)
    for i, seg in enumerate(segments):
        if seg[0] != 'line':
            continue
        x0, y0 = end_points[i - 1]
        x1, y1 = end_points[i]
        if y0 == y1:
            snapped = _nearest(y0, y_clusters, snap_distance)
            if snapped is not None:
                new_pts[i - 1] = (new_pts[i - 1][0], snapped)
                new_pts[i] = (new_pts[i][0], snapped)
        elif x0 == x1:
            snapped = _nearest(x0, x_clusters, snap_distance)
            if snapped is not None:
                new_pts[i - 1] = (snapped, new_pts[i - 1][1])
                new_pts[i] = (snapped, new_pts[i][1])

    new_segments = []
    for i, seg in enumerate(segments):
        if seg[0] == 'line':
            new_segments.append(('line', new_pts[i]))
        else:
            new_segments.append(('curve', seg[1], seg[2], new_pts[i]))
    return new_pts[-1], new_segments
