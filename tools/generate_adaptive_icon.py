#!/usr/bin/env python3
"""Generate Android launcher icon layers without changing the supplied artwork."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "icon-source"
RES = ROOT / "android-project/app/src/main/res"
DRAWABLE = RES / "drawable-nodpi"
CANVAS_SIZE = 1024


def open_rgba(path: Path) -> Image.Image:
    with Image.open(path) as image:
        return image.convert("RGBA")


def open_source_layer(path: Path, label: str) -> Image.Image:
    """Load a supplied square icon layer as-is; do not crop, pad, or scale it."""
    image = open_rgba(path)
    if image.size != (CANVAS_SIZE, CANVAS_SIZE):
        raise ValueError(
            f"{label} is {image.width}x{image.height}; expected "
            f"{CANVAS_SIZE}x{CANVAS_SIZE}. Refusing to resize or extend source artwork."
        )
    print(f"{label}: preserving original {CANVAS_SIZE}x{CANVAS_SIZE} pixels")
    return image


def save_webp(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="WEBP", lossless=True, method=6)
    print(f"Wrote {path.relative_to(ROOT)}")


def main() -> None:
    DRAWABLE.mkdir(parents=True, exist_ok=True)

    # Keep the artwork aligned exactly as supplied. In particular, do not crop
    # transparent edges or shrink the foreground to the adaptive-icon safe zone:
    # that moved the diagonal tire marks away from the edges on the previous icon.
    # Android launchers may mask/clip edge artwork differently, but preserving the
    # source composition is preferred over altering the original aesthetics.
    background = open_source_layer(SOURCE / "background.png", "background.png")
    foreground = open_source_layer(SOURCE / "foreground.png", "foreground.png")
    monochrome = open_source_layer(SOURCE / "monochrome.png", "monochrome.png")

    save_webp(foreground, DRAWABLE / "ic_launcher_logo.webp")
    save_webp(monochrome, DRAWABLE / "ic_launcher_monochrome.webp")

    # Keep Play Store artwork synchronized with the unchanged green/yellow
    # background and the full-size original foreground layer.
    combined = Image.alpha_composite(background, foreground)
    store_icon = combined.resize((512, 512), Image.Resampling.LANCZOS)
    save_webp(store_icon, ROOT / "android-project/app/src/main/ic_launcher-playstore.webp")

    # Preview artifacts allow checking both the full square artwork and a
    # circular mask without modifying either supplied source layer.
    preview = combined.resize((512, 512), Image.Resampling.LANCZOS)
    preview.save(ROOT / "adaptive-icon-preview.png", format="PNG", optimize=True)
    round_mask = Image.new("L", preview.size, 0)
    ImageDraw.Draw(round_mask).ellipse((0, 0, 511, 511), fill=255)
    round_preview = preview.copy()
    round_preview.putalpha(round_mask)
    round_preview.save(ROOT / "adaptive-icon-preview-round.png", format="PNG", optimize=True)
    print("Wrote square and circular-mask icon previews.")


if __name__ == "__main__":
    main()
