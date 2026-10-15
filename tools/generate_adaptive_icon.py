#!/usr/bin/env python3
"""Generate launcher icon layers from the PNGs in icon-source/ before Gradle."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "icon-source"
RES = ROOT / "android-project/app/src/main/res"
DRAWABLE = RES / "drawable-nodpi"
CANVAS_SIZE = 1024
# The adaptive icon's central safe area is about 61% of its full layer.
# Use 60% to leave a little extra room for circular and squircle launcher masks.
SAFE_ART_FRACTION = 0.60


def open_rgba(path: Path) -> Image.Image:
    with Image.open(path) as image:
        return image.convert("RGBA")


def trim_alpha(image: Image.Image, label: str) -> Image.Image:
    alpha = image.getchannel("A")
    if alpha.getextrema()[0] == 255:
        raise ValueError(
            f"{label} has no transparent pixels; refusing to use it as a "
            "foreground layer because it could cover the background."
        )
    # Ignore negligible alpha noise when finding the visible artwork bounds.
    bounds = alpha.point(lambda p: 255 if p > 2 else 0).getbbox()
    if not bounds:
        raise ValueError(f"{label} contains no visible artwork.")
    return image.crop(bounds)


def fit_foreground(path: Path, label: str) -> Image.Image:
    source = trim_alpha(open_rgba(path), label)
    max_extent = round(CANVAS_SIZE * SAFE_ART_FRACTION)
    scale = min(max_extent / source.width, max_extent / source.height, 1.0)
    out_w = max(1, round(source.width * scale))
    out_h = max(1, round(source.height * scale))
    artwork = source.resize((out_w, out_h), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (CANVAS_SIZE, CANVAS_SIZE), (0, 0, 0, 0))
    canvas.alpha_composite(
        artwork,
        ((CANVAS_SIZE - out_w) // 2, (CANVAS_SIZE - out_h) // 2),
    )
    print(
        f"{label}: cropped visible artwork {source.width}x{source.height}; "
        f"scaled to {out_w}x{out_h} on a {CANVAS_SIZE}x{CANVAS_SIZE} canvas"
    )
    return canvas


def save_webp(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="WEBP", lossless=True, method=6)
    print(f"Wrote {path.relative_to(ROOT)}")


def main() -> None:
    DRAWABLE.mkdir(parents=True, exist_ok=True)
    background = ImageOps.fit(
        open_rgba(SOURCE / "background.png"),
        (CANVAS_SIZE, CANVAS_SIZE),
        method=Image.Resampling.LANCZOS,
        centering=(0.5, 0.5),
    )
    # Do not let transparent pixels in the supplied background show through.
    if background.getchannel("A").getextrema()[0] < 255:
        matte = Image.new("RGBA", background.size, (37, 143, 65, 255))
        matte.alpha_composite(background)
        background = matte

    foreground = fit_foreground(SOURCE / "foreground.png", "foreground.png")
    monochrome = fit_foreground(SOURCE / "monochrome.png", "monochrome.png")

    save_webp(foreground, DRAWABLE / "ic_launcher_logo.webp")
    save_webp(monochrome, DRAWABLE / "ic_launcher_monochrome.webp")

    # Keep the Play Store artwork synchronized with the same green background.
    combined = Image.alpha_composite(background, foreground)
    store_icon = combined.resize((512, 512), Image.Resampling.LANCZOS)
    save_webp(store_icon, ROOT / "android-project/app/src/main/ic_launcher-playstore.webp")

    # Preview artifacts let the design be checked before installing the APK.
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
