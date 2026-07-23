"""Extract continuous-valued glyph-cell fields for interpolated contour
tracing (see contours.py) -- preserves the source TGA's real antialiasing
instead of collapsing it to a hard-thresholded binary mask."""


def alpha_grid(cell):
    """Vanilla hudchars: RGB is flat white, shape lives entirely in alpha.
    Returns a grid (list of rows) of float alpha values 0..255."""
    grid = []
    for row in cell.rows:
        grid.append([float(row[x * 4 + 3]) for x in range(cell.width)])
    return grid
