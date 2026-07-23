"""Read uncompressed 32/24bpp TGA atlases and slice them into a grid of
glyph cells."""
import struct


class TgaImage:
    def __init__(self, width, height, rows):
        self.width = width
        self.height = height
        self.rows = rows  # list of bytes, top-to-bottom, each row width*4 bytes RGBA

    def get_pixel(self, x, y):
        i = x * 4
        row = self.rows[y]
        return row[i], row[i + 1], row[i + 2], row[i + 3]


def parse_tga_bytes(data):
    id_length = data[0]
    colormap_type = data[1]
    image_type = data[2]
    width = struct.unpack('<H', data[12:14])[0]
    height = struct.unpack('<H', data[14:16])[0]
    bpp = data[16]
    descriptor = data[17]

    if colormap_type != 0:
        raise ValueError("colormapped TGA not supported")
    if image_type != 2:
        raise ValueError(f"unsupported TGA image type {image_type} (need uncompressed true-color)")
    if bpp not in (24, 32):
        raise ValueError(f"unsupported TGA bit depth {bpp}")

    offset = 18 + id_length
    bytespp = bpp // 8
    pixels = data[offset:offset + width * height * bytespp]
    top_to_bottom = bool(descriptor & 0x20)

    rows = []
    for y in range(height):
        start = y * width * bytespp
        raw_row = pixels[start:start + width * bytespp]
        row = bytearray(width * 4)
        for x in range(width):
            i = x * bytespp
            b = raw_row[i]
            g = raw_row[i + 1]
            r = raw_row[i + 2]
            a = raw_row[i + 3] if bytespp == 4 else 255
            row[x * 4:x * 4 + 4] = bytes([r, g, b, a])
        rows.append(bytes(row))

    if not top_to_bottom:
        rows.reverse()

    return TgaImage(width, height, rows)


def read_tga(path):
    with open(path, 'rb') as f:
        return parse_tga_bytes(f.read())


def slice_grid(image, grid_size=16):
    """Split a (grid_size*cell)x(grid_size*cell) atlas into grid_size**2
    cells, row-major (cell 0 = top-left)."""
    cell_w = image.width // grid_size
    cell_h = image.height // grid_size
    cells = []
    for row in range(grid_size):
        for col in range(grid_size):
            cell_rows = []
            for y in range(cell_h):
                src_row = image.rows[row * cell_h + y]
                start = col * cell_w * 4
                cell_rows.append(src_row[start:start + cell_w * 4])
            cells.append(TgaImage(cell_w, cell_h, cell_rows))
    return cells
