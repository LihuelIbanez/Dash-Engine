#!/usr/bin/env python3
"""Generate a DashEngine app icon (.icns) from the project logo PNG
using macOS sips/iconutil tools."""

import subprocess, os, sys, shutil


def main():
    project_dir = os.path.dirname(os.path.abspath(__file__))
    src_png = os.path.join(project_dir, "..", "assets", "icons", "dashengine.png")
    src_png = os.path.normpath(src_png)

    if not os.path.exists(src_png):
        print(f"Error: logo PNG not found at {src_png}", file=sys.stderr)
        sys.exit(1)

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
        out = os.path.join(iconset_dir, name)
        subprocess.check_call(["/usr/bin/sips", "-z", str(sz), str(sz), src_png, "--out", out],
                              stdout=subprocess.DEVNULL)
        print(f"  Resized {name} ({sz}x{sz})")

    # Convert to .icns using iconutil
    icns_path = os.path.join(project_dir, "DashEngine.icns")
    subprocess.check_call(["/usr/bin/iconutil", "-c", "icns", iconset_dir, "-o", icns_path])
    print(f"Icon created: {icns_path}")

    # Cleanup iconset
    shutil.rmtree(iconset_dir)

if __name__ == "__main__":
    main()
