from metrics import proportional_metrics


def test_glyph_with_ink_keeps_its_position_and_gets_a_tight_advance():
    # ink spans x=[100, 200] -- the loops must come back completely
    # unmodified (the runtime bake renders these same coordinates directly,
    # see metrics.py's module docstring), only advance/lsb are derived
    loops = [((100, 900), [('line', (200, 900)), ('line', (200, 800)), ('line', (100, 800))])]
    contours = {'cellA': loops}

    result = proportional_metrics(contours, units_per_em=1024, right_bearing=64)

    result_loops, advance_width, lsb = result['cellA']
    assert result_loops is loops
    assert lsb == 100
    assert advance_width == 200 + 64


def test_empty_glyph_gets_default_space_width_and_zero_lsb():
    contours = {'space': []}

    result = proportional_metrics(contours, units_per_em=1024)

    loops, advance_width, lsb = result['space']
    assert loops == []
    assert lsb == 0
    assert advance_width == 1024 // 3


def test_empty_glyph_honors_explicit_space_width():
    contours = {'space': []}

    result = proportional_metrics(contours, units_per_em=1024, space_width=300)

    _, advance_width, _ = result['space']
    assert advance_width == 300
