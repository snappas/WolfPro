from tga_reader import TgaImage
from masks import alpha_grid


def _row(*pixels):
    b = bytearray()
    for p in pixels:
        b += bytes(p)
    return bytes(b)


def test_alpha_grid_basic():
    rows = [
        _row((255, 255, 255, 255), (0, 0, 0, 0)),
        _row((255, 255, 255, 130), (255, 255, 255, 100)),
    ]
    cell = TgaImage(2, 2, rows)
    assert alpha_grid(cell) == [[255.0, 0.0], [130.0, 100.0]]
