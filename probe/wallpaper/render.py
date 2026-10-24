#!/usr/bin/env python3
"""Render a wallpaper page to an exact 480x800 JPEG through headless Chromium.

The page draws at 3x device pixels and sets window.__done when finished; the
capture is downsampled with Lanczos, given a light monochrome grain so the
gradients survive JPEG and the panel without banding, and saved as sRGB JPEG.

usage: render.py page.html out.jpg
"""
import io
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

W, H, SS = 480, 800, 3


def render(page_path: Path, out: Path, grain: float = 1.4) -> None:
    with sync_playwright() as p:
        browser = p.chromium.launch(args=["--force-color-profile=srgb", "--disable-lcd-text"])
        page = browser.new_page(viewport={"width": W, "height": H}, device_scale_factor=SS)
        page.on("console", lambda m: print("[page]", m.text))
        page.on("pageerror", lambda e: print("[page error]", e))
        page.goto(page_path.resolve().as_uri())
        page.wait_for_function("window.__done === true", timeout=120_000)
        png = page.screenshot(clip={"x": 0, "y": 0, "width": W, "height": H}, type="png")
        browser.close()

    im = Image.open(io.BytesIO(png)).convert("RGB").resize((W, H), Image.LANCZOS)
    arr = np.asarray(im).astype(np.float32)
    noise = np.random.default_rng(55).normal(0, grain, (H, W, 1))
    arr = np.clip(arr + noise, 0, 255).astype(np.uint8)
    Image.fromarray(arr, "RGB").save(out, "JPEG", quality=88, subsampling=0, optimize=True)


if __name__ == "__main__":
    render(Path(sys.argv[1]), Path(sys.argv[2]))
    print(sys.argv[2])
