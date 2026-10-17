#!/usr/bin/env python3
"""Generate Android launcher icon layers without changing the supplied artwork."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "icon-source"
RES = ROOT / "android-project/app/src/main/res"
DRAWABLE = RES / "drawable-nodpi"
CANVAS_SIZE = 1024
# Use a 22.5% reduction as an intermediate size trial. Leave the supplied
# background untouched.
FOREGROUND_SCALE = 0.775


def open_rgba(path: Path) -> Image.Image:
    with Image.open(path) as image:
        return image.convert("RGBA")


def open_source_layer(path: Path, label: str) -> Image.Image:
    """Load a supplied square icon layer without cropping or extending it."""
    image = open_rgba(path)
    if image.size != (CANVAS_SIZE, CANVAS_SIZE):
        raise ValueError(
            f"{label} is {image.width}x{image.height}; expected "
            f"{CANVAS_SIZE}x{CANVAS_SIZE}. Refusing to resize or extend source artwork."
        )
    return image


def scale_foreground_layer(image: Image.Image, label: str) -> Image.Image:
    """Center the original art at a slightly reduced scale on transparency."""
    scaled_size = round(CANVAS_SIZE * FOREGROUND_SCALE)
    artwork = image.resize(
        (scaled_size, scaled_size),
        Image.Resampling.LANCZOS,
    )
    canvas = Image.new("RGBA", (CANVAS_SIZE, CANVAS_SIZE), (0, 0, 0, 0))
    offset = (CANVAS_SIZE - scaled_size) // 2
    canvas.alpha_composite(artwork, (offset, offset))
    print(
        f"{label}: scaled to {scaled_size}x{scaled_size} "
        f"({FOREGROUND_SCALE:.0%}) and centered"
    )
    return canvas


def save_webp(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="WEBP", lossless=True, method=6)
    print(f"Wrote {path.relative_to(ROOT)}")


def main() -> None:
    DRAWABLE.mkdir(parents=True, exist_ok=True)

    # Keep the supplied green/yellow background pixel-for-pixel. Scale only
    # the pink and monochrome artwork by 15% as an in-between size trial: this
    # should be less zoomed-in than the full-size version while retaining much
    # more detail than the previous 60% safe-area fit.
    background = open_source_layer(SOURCE / "background.png", "background.png")
    foreground_source = open_source_layer(SOURCE / "foreground.png", "foreground.png")
    monochrome_source = open_source_layer(SOURCE / "monochrome.png", "monochrome.png")
    foreground = scale_foreground_layer(foreground_source, "foreground.png")
    monochrome = scale_foreground_layer(monochrome_source, "monochrome.png")

    save_webp(foreground, DRAWABLE / "ic_launcher_logo.webp")
    save_webp(monochrome, DRAWABLE / "ic_launcher_monochrome.webp")

    # Keep Play Store artwork synchronized with the unchanged green/yellow
    # background and the moderately scaled original foreground.
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
