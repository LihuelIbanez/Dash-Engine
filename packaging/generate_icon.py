#!/usr/bin/env python3
"""Generate a DashEngine app icon (.icns) using only the macOS sips/iconutil tools
and a programmatically-created PNG via the built-in Quartz / CoreGraphics APIs."""

import subprocess, tempfile, os, sys, math

def create_png(path, size):
    """Create a simple DashEngine icon PNG at the given size."""
    import ctypes, ctypes.util

    # Load CoreGraphics
    cg_path = ctypes.util.find_library("CoreGraphics")
    cg = ctypes.cdll.LoadLibrary(cg_path)

    # Constants
    kCGColorSpaceGenericRGB = b"kCGColorSpaceGenericRGB"
    kCGImageAlphaPremultipliedLast = 1
    kCGPathFill = 0

    # Colour space
    cg.CGColorSpaceCreateWithName.restype = ctypes.c_void_p
    cg.CGColorSpaceCreateWithName.argtypes = [ctypes.c_void_p]
    from ctypes import c_void_p, c_size_t, c_uint32, c_int, c_float
    import CoreFoundation  # noqa – available on macOS Python
    # Fallback: use pure-Python PNG writer (minimal, no dependencies)
    _write_minimal_png(path, size)

def _write_minimal_png(path, size):
    """Write a PNG icon using pure Python (no dependencies)."""
    import struct, zlib

    pixels = []
    cx, cy = size / 2, size / 2
    r = size * 0.38   # diamond radius

    for y in range(size):
        row = []
        for x in range(size):
            # Normalised coords
            nx = (x - cx) / r
            ny = (y - cy) / r

            # Diamond shape: |nx| + |ny| <= 1
            d = abs(nx) + abs(ny)

            if d <= 1.0:
                # Inside diamond – dark green gradient
                t = 1.0 - d
                red   = int(20 + 40 * t)
                green = int(80 + 120 * t)
                blue  = int(40 + 60 * t)

                # Top-face highlight
                if ny < 0:
                    f = min(1.0, abs(ny) * 1.5)
                    red   = int(red   + (255 - red)   * f * 0.15)
                    green = int(green + (255 - green) * f * 0.20)
                    blue  = int(blue  + (255 - blue)  * f * 0.10)

                # "D" letter in center
                lx = (x - cx) / (size * 0.12)
                ly = (y - cy) / (size * 0.20)
                in_d_letter = False

                # D stem (left bar)
                if -2.0 <= ly <= 2.0 and -1.5 <= lx <= -0.8:
                    in_d_letter = True
                # D curve (right arc)
                arc_dist = math.sqrt(lx * lx + ly * ly)
                if 1.0 < arc_dist < 1.8 and lx > -0.3:
                    in_d_letter = True

                if in_d_letter:
                    red, green, blue = 240, 240, 220

                alpha = 255
            elif d <= 1.06:
                # Border ring
                red, green, blue, alpha = 140, 200, 100, 255
            else:
                red, green, blue, alpha = 0, 0, 0, 0

            # Outer rounded-rect background shadow
            margin = size * 0.08
            rx = abs(x - cx)
            ry = abs(y - cy)
            corner_r = size * 0.18
            half = size / 2 - margin
            in_rect = True
            if rx > half or ry > half:
                in_rect = False
            elif rx > half - corner_r and ry > half - corner_r:
                cr_dist = math.sqrt((rx - (half - corner_r))**2 + (ry - (half - corner_r))**2)
                if cr_dist > corner_r:
                    in_rect = False

            if d > 1.06 and in_rect:
                # Dark background behind diamond
                red, green, blue, alpha = 30, 30, 35, 255

            row.extend([red, green, blue, alpha])
        pixels.append(bytes([0] + row))  # filter byte + row

    raw = b''.join(pixels)
    compressed = zlib.compress(raw)

    def chunk(ctype, data):
        c = ctype + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)

    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = sig + chunk(b'IHDR', ihdr) + chunk(b'IDAT', compressed) + chunk(b'IEND', b'')

    with open(path, 'wb') as f:
        f.write(png)

def main():
    project_dir = os.path.dirname(os.path.abspath(__file__))
    iconset_dir = os.path.join(project_dir, "DashEngine.iconset")
    os.makedirs(iconset_dir, exist_ok=True)

    # Required icon sizes for .icns
    sizes = [
        (16,   "icon_16x16.png"),
        (32,   "icon_16x16@2x.png"),
        (32,   "icon_32x32.png"),
        (64,   "icon_32x32@2x.png"),
        (128,  "icon_128x128.png"),
        (256,  "icon_128x128@2x.png"),
        (256,  "icon_256x256.png"),
        (512,  "icon_256x256@2x.png"),
        (512,  "icon_512x512.png"),
        (1024, "icon_512x512@2x.png"),
    ]

    for sz, name in sizes:
        _write_minimal_png(os.path.join(iconset_dir, name), sz)
        print(f"  Generated {name} ({sz}x{sz})")

    # Convert to .icns using iconutil
    icns_path = os.path.join(project_dir, "DashEngine.icns")
    subprocess.check_call(["/usr/bin/iconutil", "-c", "icns", iconset_dir, "-o", icns_path])
    print(f"Icon created: {icns_path}")

    # Cleanup iconset
    import shutil
    shutil.rmtree(iconset_dir)

if __name__ == "__main__":
    main()
