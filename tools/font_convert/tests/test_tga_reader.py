import struct

from tga_reader import parse_tga_bytes, slice_grid


def _make_tga_bytes(width, height, pixel_rows_rgba, top_to_bottom):
    header = bytearray(18)
    header[2] = 2  # uncompressed true-color
    header[12:14] = struct.pack('<H', width)
    header[14:16] = struct.pack('<H', height)
    header[16] = 32
    header[17] = 0x20 if top_to_bottom else 0x00

    rows = pixel_rows_rgba if top_to_bottom else list(reversed(pixel_rows_rgba))
    body = bytearray()
    for row in rows:
        for (r, g, b, a) in row:
            body += bytes([b, g, r, a])  # TGA stores BGRA
    return bytes(header) + bytes(body)


def test_read_top_to_bottom():
    pixels = [
        [(255, 0, 0, 255), (0, 255, 0, 128)],
        [(0, 0, 255, 0), (10, 20, 30, 40)],
    ]
    data = _make_tga_bytes(2, 2, pixels, top_to_bottom=True)
    img = parse_tga_bytes(data)
    assert img.width == 2 and img.height == 2
    assert img.get_pixel(0, 0) == (255, 0, 0, 255)
    assert img.get_pixel(1, 0) == (0, 255, 0, 128)
    assert img.get_pixel(0, 1) == (0, 0, 255, 0)
    assert img.get_pixel(1, 1) == (10, 20, 30, 40)


def test_read_bottom_to_top():
    pixels = [
        [(255, 0, 0, 255), (0, 255, 0, 128)],
        [(0, 0, 255, 0), (10, 20, 30, 40)],
    ]
    data = _make_tga_bytes(2, 2, pixels, top_to_bottom=False)
    img = parse_tga_bytes(data)
    assert img.get_pixel(0, 0) == (255, 0, 0, 255)
    assert img.get_pixel(1, 1) == (10, 20, 30, 40)


def test_slice_grid():
    pixels = [
        [(1, 0, 0, 255)] * 2 + [(2, 0, 0, 255)] * 2,
        [(1, 0, 0, 255)] * 2 + [(2, 0, 0, 255)] * 2,
        [(3, 0, 0, 255)] * 2 + [(4, 0, 0, 255)] * 2,
        [(3, 0, 0, 255)] * 2 + [(4, 0, 0, 255)] * 2,
    ]
    data = _make_tga_bytes(4, 4, pixels, top_to_bottom=True)
    img = parse_tga_bytes(data)
    cells = slice_grid(img, grid_size=2)
    assert len(cells) == 4
    assert cells[0].get_pixel(0, 0) == (1, 0, 0, 255)
    assert cells[1].get_pixel(0, 0) == (2, 0, 0, 255)
    assert cells[2].get_pixel(0, 0) == (3, 0, 0, 255)
    assert cells[3].get_pixel(0, 0) == (4, 0, 0, 255)
