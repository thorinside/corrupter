#!/usr/bin/env python3
"""Render VCV Rack panel SVGs to MetaModule faceplate PNGs.

MetaModule faceplates are 240 px tall (47.44 dpi for a 128.5 mm Eurorack
panel) with an opaque background. This script rasterises every ``*.svg`` in the
source directory with Inkscape and writes ``<name>.png`` beside the other
assets, which is the layout ``create_plugin(SOURCE_ASSETS ...)`` expects
(``res/Foo.svg`` in Rack becomes ``Foo.png`` at the asset root).

The Corrupter panel draws its labels at runtime with NanoVG (see
``CorrupterLabels`` in ``Corrupter.cpp``). MetaModule would treat that overlay
as a full-panel dynamic display, so the same labels are injected here as SVG
``<text>`` elements before rasterising. Keep the two tables in sync.

Usage:
    bake_panels.py --res <dir with svgs> --out <assets dir> [--only Corrupter]

Requires Inkscape 1.x on PATH (or INKSCAPE_BIN_PATH). DejaVu Sans is located
via $DEJAVU_TTF, a local VCV Rack install, or fontconfig.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

PANEL_HEIGHT_PX = 240
RACK_PX_TO_MM = 25.4 / 75.0  # Rack draws at 75 dpi

SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)

# Mirror of CorrupterLabels::drawLayer (positions in mm, sizes in Rack px).
# (text, x_mm, y_mm_bottom, font_px, colour)
_X3 = (25.0, 45.72, 66.44)
_COL = (17.0, 35.48, 53.96, 72.44)
_KNOB = "#90b0d0"
_IO = "#90b0d0"
_BOTTOM = "#7090b0"
CORRUPTER_LABELS = [
    ("IN L", 10.0, 12.8, 8, _IO),
    ("IN R", 20.0, 12.8, 8, _IO),
    ("CLOCK", 35.0, 12.8, 8, _IO),
    ("OUT L", 71.44, 12.8, 8, _IO),
    ("OUT R", 81.44, 12.8, 8, _IO),
    ("TIME", _X3[0], 28.0, 9, _KNOB),
    ("REPEATS", _X3[1], 28.0, 9, _KNOB),
    ("MIX", _X3[2], 28.0, 9, _KNOB),
    ("BEND", _X3[0], 55.5, 9, _KNOB),
    ("BREAK", _X3[1], 55.5, 9, _KNOB),
    ("CORRUPT", _X3[2], 55.5, 9, _KNOB),
    ("BEND", _COL[0], 98.0, 9, _KNOB),
    ("BREAK", _COL[1], 98.0, 9, _KNOB),
    ("FREEZE", _COL[2], 98.0, 9, _KNOB),
    ("ALGO", _COL[3], 98.0, 9, _KNOB),
    ("GW", _COL[3], 105.5, 8, _BOTTOM),
    ("MODE", _X3[0], 115.8, 8, _BOTTOM),
    ("SLNC", _X3[1], 115.8, 8, _BOTTOM),
    ("ST MODE", _X3[2], 115.8, 8, _BOTTOM),
]

LABEL_TABLES = {"Corrupter": CORRUPTER_LABELS}


def find_inkscape() -> str:
    env = os.environ.get("INKSCAPE_BIN_PATH")
    if env and Path(env).exists():
        return env
    for cand in ("inkscape", "/Applications/Inkscape.app/Contents/MacOS/inkscape"):
        found = shutil.which(cand) or (cand if Path(cand).exists() else None)
        if found:
            return found
    sys.exit("inkscape not found; install it or set INKSCAPE_BIN_PATH")


def find_dejavu() -> Path | None:
    env = os.environ.get("DEJAVU_TTF")
    if env and Path(env).exists():
        return Path(env)
    candidates = [
        "/Applications/Rack.app/Contents/Resources/res/fonts/DejaVuSans.ttf",
        "/Applications/VCV Rack 2 Pro.app/Contents/Resources/res/fonts/DejaVuSans.ttf",
        "/Applications/VCV Rack 2 Free.app/Contents/Resources/res/fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    ]
    for c in candidates:
        if Path(c).exists():
            return Path(c)
    if shutil.which("fc-match"):
        out = subprocess.run(
            ["fc-match", "-f", "%{file}", "DejaVu Sans"], capture_output=True, text=True
        ).stdout.strip()
        if out and "DejaVu" in out and Path(out).exists():
            return Path(out)
    return None


def inject_labels(svg_path: Path, labels, scale: float, out_path: Path) -> None:
    tree = ET.parse(svg_path)
    root = tree.getroot()
    group = ET.SubElement(root, f"{{{SVG_NS}}}g", {"id": "metamodule-labels"})
    for text, x_mm, y_bottom_mm, font_px, colour in labels:
        size_mm = font_px * RACK_PX_TO_MM * scale
        # nvg ALIGN_BOTTOM anchors the descender line; DejaVu's descent is ~0.24 em.
        baseline_mm = y_bottom_mm - 0.24 * size_mm
        el = ET.SubElement(
            group,
            f"{{{SVG_NS}}}text",
            {
                "x": f"{x_mm:.3f}",
                "y": f"{baseline_mm:.3f}",
                "font-family": "DejaVu Sans, sans-serif",
                "font-size": f"{size_mm:.3f}",
                "fill": colour,
                "text-anchor": "middle",
            },
        )
        el.text = text
    tree.write(out_path, xml_declaration=True, encoding="UTF-8")


def panel_background(svg_path: Path) -> str:
    """First <rect fill> in the document, used to flood any transparent pixels."""
    try:
        for el in ET.parse(svg_path).getroot().iter(f"{{{SVG_NS}}}rect"):
            fill = el.get("fill")
            if fill and fill.startswith("#"):
                return fill
    except ET.ParseError:
        pass
    return "#000000"


def render(inkscape: str, svg: Path, png: Path, background: str, env: dict) -> None:
    cmd = [
        inkscape,
        str(svg),
        "--export-type=png",
        f"--export-filename={png}",
        f"--export-height={PANEL_HEIGHT_PX}",
        f"--export-background={background}",
        "--export-background-opacity=1.0",
    ]
    subprocess.run(cmd, check=True, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--res", required=True, type=Path, help="directory containing the Rack panel SVGs")
    ap.add_argument("--out", required=True, type=Path, help="MetaModule assets directory to write PNGs into")
    ap.add_argument("--only", action="append", default=[], help="panel stem(s) to render (default: all)")
    ap.add_argument("--label-scale", type=float, default=1.0,
                    help="enlarge baked labels for the 240 px panel (default 1.0; 1.2 crowds the I/O row)")
    args = ap.parse_args()

    inkscape = find_inkscape()
    svgs = sorted(p for p in args.res.glob("*.svg") if not args.only or p.stem in args.only)
    if not svgs:
        sys.exit(f"no SVGs found in {args.res}")
    args.out.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        needs_font = any(p.stem in LABEL_TABLES for p in svgs)
        if needs_font:
            ttf = find_dejavu()
            if ttf is None:
                sys.exit("DejaVuSans.ttf not found; set DEJAVU_TTF=/path/to/DejaVuSans.ttf")
            fontdir = tmpdir / "fonts"
            fontdir.mkdir()
            shutil.copy(ttf, fontdir / "DejaVuSans.ttf")
            conf = tmpdir / "fonts.conf"
            conf.write_text(
                "<?xml version='1.0'?><!DOCTYPE fontconfig SYSTEM 'fonts.dtd'>"
                f"<fontconfig><dir>{fontdir}</dir><cachedir>{tmpdir / 'fc-cache'}</cachedir></fontconfig>"
            )
            env["FONTCONFIG_FILE"] = str(conf)

        for svg in svgs:
            src = svg
            if svg.stem in LABEL_TABLES:
                src = tmpdir / f"{svg.stem}-labelled.svg"
                inject_labels(svg, LABEL_TABLES[svg.stem], args.label_scale, src)
            png = args.out / f"{svg.stem}.png"
            render(inkscape, src, png, panel_background(svg), env)
            print(f"{svg.name} -> {png}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
