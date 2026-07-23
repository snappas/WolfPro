from alignment import align_glyph_set


def _square_loop(x0, y0, x1, y1):
    return (x0, y0), [('line', (x1, y0)), ('line', (x1, y1)), ('line', (x0, y1)), ('line', (x0, y0))]


def test_snaps_near_matching_baselines_across_glyphs_to_one_value():
    # 10 glyphs whose bottom edge is at y=10.0 exactly, one whose bottom
    # edge drifted to y=10.2 (real antialiasing noise, comfortably inside
    # SNAP_DISTANCE=0.3) -- once there are enough matching glyphs to form
    # a real cluster, the outlier should end up sharing that cluster's
    # exact value rather than sitting a fraction of a pixel apart on its
    # own
    named = {}
    for i in range(10):
        named[f'cell{i:03d}'] = [_square_loop(0.0, 0.0, 5.0, 10.0)]
    named['cellOUT'] = [_square_loop(0.0, 0.0, 5.0, 10.2)]

    result = align_glyph_set(named)

    def bottom_y(name):
        _, segments = result[name][0]
        return max(y for x, y in [seg[-1] for seg in segments])

    canonical = bottom_y('cell000')
    for i in range(1, 10):
        assert bottom_y(f'cell{i:03d}') == canonical
    assert bottom_y('cellOUT') == canonical


def test_does_not_snap_when_cluster_has_too_few_members():
    # only 2 glyphs share this baseline -- below MIN_CLUSTER_MEMBERS, so it
    # should be left alone rather than treated as a real font-wide row
    named = {
        'cellA': [_square_loop(0.0, 0.0, 5.0, 10.0)],
        'cellB': [_square_loop(0.0, 0.0, 5.0, 10.0)],
    }
    result = align_glyph_set(named)
    assert result['cellA'] == named['cellA']
    assert result['cellB'] == named['cellB']


def test_never_touches_curve_control_points():
    named = {}
    for i in range(10):
        named[f'cell{i:03d}'] = [_square_loop(0.0, 0.0, 5.0, 10.0)]
    named['cellCurve'] = [((0.0, 0.0), [
        ('curve', (1.0, 5.0), (2.0, 9.88), (3.0, 10.0)),
        ('line', (0.0, 0.0)),
    ])]

    result = align_glyph_set(named)
    _, segments = result['cellCurve'][0]
    assert segments[0] == ('curve', (1.0, 5.0), (2.0, 9.88), (3.0, 10.0))
