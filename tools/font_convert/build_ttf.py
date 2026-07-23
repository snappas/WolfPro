"""Build a .ttf from traced glyph contours using fontTools. The cmap is not
merely cosmetic -- the runtime bake in src/renderer_common/tr_font_bake.c
looks each cell's glyph up by Unicode codepoint (stbtt_FindGlyphIndex(font,
cellIndex)), so the cmap built here must map every cell index to its own
glyph for the engine to find it. (Advance widths/side bearings genuinely
are cosmetic for the engine, which positions every character in its own
fixed pixel grid regardless of what the TTF's hmtx table says -- see
metrics.py for why they're computed proportionally anyway.)

glyph_contours entries are contours.trace_field's (already font-unit-
converted) loops: (start_point, segments), each segment a tagged
('line', point) or ('curve', c1, c2, point). potrace produces cubic
Beziers, but TrueType's glyf table only supports quadratic ones -- Cu2QuPen
(shipped with fontTools, no separate dependency) wraps the TTGlyphPen and
converts curveTo calls to the nearest quadratic qCurveTo automatically,
within CURVE_FIT_MAX_ERROR font units."""
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen

CURVE_FIT_MAX_ERROR = 5


def build_font(glyph_order, glyph_contours, glyph_metrics, units_per_em, cmap, output_path, family_name):
    """glyph_metrics: dict of name -> (advanceWidth, leftSideBearing).
    Names not present (e.g. '.notdef', which keeps its own hardcoded shape
    below and is rarely actually rendered) fall back to (units_per_em, 0)."""
    fb = FontBuilder(units_per_em, isTTF=True)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap(cmap)

    glyphs = {}
    for name in glyph_order:
        tt_pen = TTGlyphPen(None)
        if name == '.notdef':
            margin = units_per_em // 10
            tt_pen.moveTo((margin, 0))
            tt_pen.lineTo((margin, units_per_em - margin))
            tt_pen.lineTo((units_per_em - margin, units_per_em - margin))
            tt_pen.lineTo((units_per_em - margin, 0))
            tt_pen.closePath()
        else:
            pen = Cu2QuPen(tt_pen, CURVE_FIT_MAX_ERROR, all_quadratic=True)
            for start, segments in glyph_contours.get(name, []):
                pen.moveTo(start)
                for seg in segments:
                    if seg[0] == 'line':
                        pen.lineTo(seg[1])
                    else:
                        pen.curveTo(seg[1], seg[2], seg[3])
                pen.closePath()
        glyphs[name] = tt_pen.glyph()

    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({name: glyph_metrics.get(name, (units_per_em, 0)) for name in glyph_order})
    fb.setupHorizontalHeader(ascent=units_per_em, descent=0)
    fb.setupOS2(sTypoAscender=units_per_em, sTypoDescender=0,
                usWinAscent=units_per_em, usWinDescent=0)
    ps_name = family_name.replace(' ', '') + '-Regular'
    fb.setupNameTable({
        "familyName": family_name,
        "styleName": "Regular",
        "uniqueFontIdentifier": f"1.000;{ps_name}",
        "fullName": f"{family_name} Regular",
        "psName": ps_name,
    })
    fb.setupPost()
    fb.save(output_path)
