from fontTools.ttLib import TTFont

from build_ttf import build_font


def test_build_and_reload(tmp_path):
    glyph_order = ['.notdef', 'cell000']
    contours = {'cell000': [((0, 1024), [('line', (64, 1024)), ('line', (64, 960)), ('line', (0, 960))])]}
    metrics = {'cell000': (512, 32)}
    out = tmp_path / "test.ttf"
    build_font(glyph_order, contours, metrics, 1024, {65: 'cell000'}, str(out), 'hudchars')

    f = TTFont(str(out))
    assert f.getGlyphOrder() == ['.notdef', 'cell000']
    g = f['glyf']['cell000']
    assert g.numberOfContours == 1
    # an all-line contour has no curves to fit, so Cu2QuPen passes every
    # point straight through unchanged
    assert list(g.coordinates) == [(0, 1024), (64, 1024), (64, 960), (0, 960)]
    assert all(flag & 0x1 for flag in g.flags)  # every point on-curve
    assert f['cmap'].getBestCmap()[65] == 'cell000'
    assert f['head'].unitsPerEm == 1024
    # per-glyph metrics dict must reach hmtx rather than falling back to a
    # uniform units_per_em-wide advance
    assert f['hmtx']['cell000'] == (512, 32)
    # '.notdef' isn't present in the metrics dict -- must fall back to
    # (units_per_em, 0) rather than KeyError
    assert f['hmtx']['.notdef'] == (1024, 0)


def test_curve_segment_produces_off_curve_points(tmp_path):
    # potrace emits cubic Beziers; TrueType only supports quadratic, so
    # Cu2QuPen must convert -- this exercises that conversion actually
    # happening rather than curves silently getting dropped or flattened
    # to lines (verified directly: a contour made only of 'line' segments,
    # as in test_build_and_reload above, comes back with every point
    # on-curve; a contour with a real curve segment must not)
    glyph_order = ['.notdef', 'cell000']
    contours = {'cell000': [
        ((0, 0), [
            ('curve', (0, 100), (100, 100), (100, 0)),
            ('curve', (100, -100), (0, -100), (0, 0)),
        ])
    ]}
    out = tmp_path / "curve.ttf"
    build_font(glyph_order, contours, {'cell000': (1024, 0)}, 1024, {65: 'cell000'}, str(out), 'hudchars')

    f = TTFont(str(out))
    g = f['glyf']['cell000']
    assert g.numberOfContours == 1
    assert any(not (flag & 0x1) for flag in g.flags)  # at least one off-curve point


def test_empty_glyph_has_no_contours(tmp_path):
    glyph_order = ['.notdef', 'cell000']
    out = tmp_path / "empty.ttf"
    build_font(glyph_order, {}, {}, 1024, {}, str(out), 'hudchars')

    f = TTFont(str(out))
    assert f['glyf']['cell000'].numberOfContours == 0


def test_name_table_has_windows_required_records(tmp_path):
    glyph_order = ['.notdef', 'cell000']
    out = tmp_path / "name.ttf"
    build_font(glyph_order, {}, {}, 1024, {}, str(out), 'hudchars_OSP1')

    f = TTFont(str(out))
    name = f['name']
    # Windows font installation/GDI requires at least family(1), subfamily(2),
    # unique identifier(3), full name(4), and PostScript name(6) to recognize
    # a file as a valid, installable font -- FontBuilder.setupNameTable only
    # creates records for the keys it's explicitly given.
    assert name.getDebugName(1) == 'hudchars_OSP1'
    assert name.getDebugName(2) == 'Regular'
    assert name.getDebugName(3) == '1.000;hudchars_OSP1-Regular'
    assert name.getDebugName(4) == 'hudchars_OSP1 Regular'
    assert name.getDebugName(6) == 'hudchars_OSP1-Regular'
