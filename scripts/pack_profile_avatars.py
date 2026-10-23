#!/usr/bin/env python3
"""Pack profile bust portraits for device firmware (RGB565A8).

Reads transparent PNG/JPEG/WebP sources from docs/product/device-ui/avatars/ (top-level only),
writes LVGL C assets to client/assets/portraits/, and updates avatars.json with
only successfully converted entries.

Idempotent: skips reconversion when source SHA-256 is unchanged and the .c file exists.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
AVATARS_DOC = REPO_ROOT / "docs" / "product" / "device-ui" / "avatars"
MANIFEST_PATH = AVATARS_DOC / "avatars.json"
SKIP_SOURCE_NAMES = frozenset({"avatars.json", "README.md"})
OUT_DIR = REPO_ROOT / "client" / "assets" / "portraits"
CAT20_SCENE = REPO_ROOT / "docs" / "product" / "device-ui" / "catalog" / "scenes" / "sign-in-roster-bust.yaml"
LAB14_CATALOG_JSON = (
    REPO_ROOT / "client" / "labs" / "lab-14-device-ui-catalog" / "generated" / "catalog.json"
)
PORTRAIT_CMAKE = OUT_DIR / "portrait_pack_sources.cmake"
CARD_BACKGROUNDS = ("#5AA0E8", "#67C587", "#E8C040", "#D875A8", "#C070E8", "#4ECDC4", "#FF6B6B", "#7AD4E8")
LAB14_PROJECT = REPO_ROOT / "client" / "labs" / "lab-14-device-ui-catalog"
LVGL_IMAGE = LAB14_PROJECT / "managed_components" / "lvgl__lvgl" / "scripts" / "LVGLImage.py"
LVGL_IMAGE_FETCH_BUILD = REPO_ROOT / "build" / "labs" / "lab-14-avatar-pack-deps"
_lvgl_image_path: Path | None = None

SCHEMA = "family-link.profile-avatars-manifest/1"
SOURCE_SUFFIXES = (".png", ".jpg", ".jpeg", ".webp")
SYMBOL_RE = re.compile(r"^[a-z][a-z0-9_-]{0,31}$")


@dataclass
class ConvertResult:
    portrait_id: str
    source: Path
    ok: bool
    message: str
    skipped: bool = False


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _discover_lvgl_image() -> Path | None:
    if LVGL_IMAGE.is_file():
        return LVGL_IMAGE
    for path in sorted(REPO_ROOT.glob("client/labs/*/managed_components/lvgl__lvgl/scripts/LVGLImage.py")):
        if path.is_file():
            return path
    return None


def _fetch_lab14_managed_components() -> None:
    from firmware import idf_environment, run
    from project_config import ProjectConfig

    config = ProjectConfig()
    config.validate()
    env, idf = idf_environment(config)
    if config.text("idf.target", required=True) != "esp32s3":
        raise FileNotFoundError("avatar pack requires idf.target=esp32s3 in host config")
    LVGL_IMAGE_FETCH_BUILD.mkdir(parents=True, exist_ok=True)
    sdkconfig = LVGL_IMAGE_FETCH_BUILD / "sdkconfig"
    cmd = idf + [
        "-C",
        str(LAB14_PROJECT),
        "-B",
        str(LVGL_IMAGE_FETCH_BUILD),
        "-DIDF_TARGET=esp32s3",
        f"-DSDKCONFIG={sdkconfig}",
        "reconfigure",
    ]
    timeout = config.integer("command.lab.build.timeout_seconds", default=1200, minimum=1)
    print(
        f"avatars: fetching LAB-14 managed components (LVGLImage.py) via idf.py reconfigure -> "
        f"{LVGL_IMAGE_FETCH_BUILD.relative_to(REPO_ROOT)}",
        file=sys.stderr,
    )
    run(cmd, env=env, timeout=timeout)


def lvgl_image_path() -> Path:
    global _lvgl_image_path
    if _lvgl_image_path is not None and _lvgl_image_path.is_file():
        return _lvgl_image_path
    found = _discover_lvgl_image()
    if found is not None:
        _lvgl_image_path = found
        return found
    try:
        _fetch_lab14_managed_components()
    except (OSError, subprocess.CalledProcessError, ImportError) as exc:
        raise FileNotFoundError(
            f"LVGLImage.py not found under {LAB14_PROJECT / 'managed_components'}. "
            "Run `make install`, then `make avatars` again (or `make lab.lab14.build` once). "
            f"Underlying error: {exc}"
        ) from exc
    found = _discover_lvgl_image()
    if found is None:
        raise FileNotFoundError(
            f"LVGLImage.py still missing after LAB-14 component fetch; expected {LVGL_IMAGE}"
        )
    _lvgl_image_path = found
    return found


def portrait_c_stem(portrait_id: str) -> str:
    """Filesystem / C identifier stem (hyphens → underscores)."""
    return portrait_id.replace("-", "_")


def portrait_lvgl_symbol(portrait_id: str) -> str:
    return f"portrait_{portrait_c_stem(portrait_id)}_candidate"


def portrait_c_asset(portrait_id: str) -> str:
    return f"portrait_{portrait_c_stem(portrait_id)}.c"


def portrait_id_from_name(path: Path) -> str:
    stem = path.stem.lower().replace(" ", "-")
    for suffix in ("-source", "_source"):
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
    stem = re.sub(r"[^a-z0-9_-]+", "-", stem).strip("-")
    if not stem or not SYMBOL_RE.match(stem):
        raise ValueError(f"invalid portrait id derived from {path.name}")
    return stem


def quant565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def expand565(pixel: int) -> tuple[int, int, int]:
    r = (pixel >> 11) & 31
    g = (pixel >> 5) & 63
    b = pixel & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def _require_pillow():
    try:
        from PIL import Image, ImageFilter  # noqa: F401
    except ImportError as exc:
        raise SystemExit("Pillow is required. Run: make install (or pip install Pillow)") from exc


def preprocess_native_rgba(source: Path, portrait_id: str, palettes: dict[str, list[str]]):
    from PIL import Image, ImageFilter

    src = Image.open(source).convert("RGBA")
    alpha_extrema = src.getchannel("A").getextrema()
    if alpha_extrema[0] != 0:
        raise ValueError("source must include transparent pixels (RGBA alpha min == 0)")

    bbox = src.getchannel("A").getbbox()
    if bbox is None:
        raise ValueError("source is fully transparent")

    crop = src.crop(bbox)
    crop.thumbnail((106, 96), Image.Resampling.LANCZOS)
    native = Image.new("RGBA", (crop.width + 4, 100))
    native.alpha_composite(crop, (2, 98 - crop.height))

    alpha = native.getchannel("A")
    hard = alpha.point(lambda a: 255 if a >= 128 else 0)
    inner = hard.filter(ImageFilter.MinFilter(3))
    outer = hard.filter(ImageFilter.MaxFilter(3))
    softened = []
    for a, inside, near in zip(alpha.getdata(), inner.getdata(), outer.getdata()):
        if inside:
            softened.append(255)
        elif not near or a < 12:
            softened.append(0)
        elif a > 243:
            softened.append(255)
        else:
            softened.append(a)
    alpha.putdata(softened)

    hex_colors = palettes.get(portrait_id)
    if hex_colors:
        flat = [tuple(bytes.fromhex(c.lstrip("#"))) for c in hex_colors]
        palette_img = Image.new("P", (1, 1))
        palette_img.putpalette([v for c in (flat + [flat[-1]] * (256 - len(flat))) for v in c])
        rgb = native.convert("RGB").quantize(palette=palette_img, dither=Image.Dither.NONE).convert("RGB")
    else:
        rgb = native.convert("RGB").quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert(
            "RGB"
        )

    rgb.putdata([expand565(quant565(*c)) for c in rgb.getdata()])
    native = rgb.convert("RGBA")
    native.putalpha(alpha)
    if native.width > 110 or native.height > 110:
        raise ValueError(f"native canvas {native.width}x{native.height} exceeds 110x110 bust limit")
    return native


def run_lvgl_convert(native_png: Path, portrait_id: str, out_dir: Path) -> Path:
    lvgl_image = lvgl_image_path()

    symbol = portrait_lvgl_symbol(portrait_id)
    out_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            sys.executable,
            str(lvgl_image),
            "--cf",
            "RGB565A8",
            "--ofmt",
            "C",
            "--compress",
            "NONE",
            "--align",
            "1",
            "--name",
            symbol,
            "-o",
            str(out_dir),
            str(native_png),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    generated = out_dir / f"{symbol}.c"
    stable = out_dir / portrait_c_asset(portrait_id)
    stable.write_text(generated.read_text(encoding="utf-8"), encoding="utf-8")
    generated.unlink(missing_ok=True)
    return stable


def verify_rgb565a8_blob(native, blob: bytes) -> None:
    magic, cf, flags, w, h, stride, _res = struct.unpack("<BBHHHHH", blob[:12])
    pixels = list(native.getdata())
    expected = b"".join(struct.pack("<H", quant565(*c[:3])) for c in pixels) + bytes(c[3] for c in pixels)
    if (magic, cf, flags, w, h, stride) != (0x19, 0x14, 0, native.width, native.height, native.width * 2):
        raise ValueError("LVGL binary header mismatch")
    if blob[12:] != expected:
        raise ValueError("LVGL binary payload mismatch")


def write_registry(manifest: dict) -> None:
    entries = sorted(manifest.get("portraits", {}).items())
    lines_h = [
        "#pragma once",
        "#include \"lvgl.h\"",
        "",
        "const lv_image_dsc_t *fl_portrait_lookup(const char *portrait_id);",
        "",
    ]
    for portrait_id, _meta in entries:
        symbol = _meta["symbol"]
        lines_h.append(f"extern const lv_image_dsc_t {symbol};")
    lines_h.append("")

    lines_c = [
        '#include "portraits_registry.h"',
        "#include <string.h>",
        "",
    ]
    for portrait_id, meta in entries:
        lines_c.append(f'extern const lv_image_dsc_t {meta["symbol"]};')
    lines_c.append("")
    lines_c.append("typedef struct {")
    lines_c.append("    const char *id;")
    lines_c.append("    const lv_image_dsc_t *image;")
    lines_c.append("} fl_portrait_entry_t;")
    lines_c.append("")
    lines_c.append("static const fl_portrait_entry_t s_portraits[] = {")
    for portrait_id, meta in entries:
        lines_c.append(f'    {{"{portrait_id}", &{meta["symbol"]}}},')
    lines_c.append("};")
    lines_c.append("")
    lines_c.append("const lv_image_dsc_t *fl_portrait_lookup(const char *portrait_id)")
    lines_c.append("{")
    lines_c.append("    if (portrait_id == NULL || portrait_id[0] == '\\0') {")
    lines_c.append("        return NULL;")
    lines_c.append("    }")
    lines_c.append("    for (size_t i = 0; i < sizeof(s_portraits) / sizeof(s_portraits[0]); i++) {")
    lines_c.append("        if (strcmp(portrait_id, s_portraits[i].id) == 0) {")
    lines_c.append("            return s_portraits[i].image;")
    lines_c.append("        }")
    lines_c.append("    }")
    lines_c.append("    return NULL;")
    lines_c.append("}")
    lines_c.append("")

    (OUT_DIR / "portraits_registry.h").write_text("\n".join(lines_h), encoding="utf-8")
    (OUT_DIR / "portraits_registry.c").write_text("\n".join(lines_c), encoding="utf-8")
    write_portrait_cmake_sources(entries)


def write_portrait_cmake_sources(entries: list[tuple[str, dict]]) -> None:
    lines = [
        "# Auto-generated by make avatars. Do not edit.",
        "set(FL_PORTRAIT_PACK_SRCS",
    ]
    for _portrait_id, meta in entries:
        c_rel = meta.get("c_asset") or portrait_c_asset(_portrait_id)
        lines.append(f'  "${{PORTRAIT_DIR}}/{c_rel}"')
    lines.append(")")
    PORTRAIT_CMAKE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def pack_lab14_catalog() -> None:
    """Keep embedded LAB-14 catalog.json aligned with CAT-20 YAML (portrait ids)."""
    pack_script = REPO_ROOT / "scripts" / "device_ui_catalog.py"
    catalog_src = REPO_ROOT / "docs" / "product" / "device-ui" / "catalog"
    subprocess.run(
        [
            sys.executable,
            str(pack_script),
            "--catalog",
            str(catalog_src),
            "--out",
            str(LAB14_CATALOG_JSON),
            "pack",
        ],
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )


def verify_cat20_catalog(portrait_ids: set[str]) -> None:
    if not LAB14_CATALOG_JSON.is_file():
        raise RuntimeError(f"LAB-14 catalog missing after pack: {LAB14_CATALOG_JSON}")
    cat = json.loads(LAB14_CATALOG_JSON.read_text(encoding="utf-8"))
    bust = next((scene for scene in cat.get("scenes", []) if scene.get("id") == "CAT-20"), None)
    if bust is None:
        raise RuntimeError("CAT-20 missing from packed catalog.json")
    users = bust.get("slots", {}).get("lower", {}).get("props", {}).get("users", [])
    if len(users) != len(portrait_ids):
        raise RuntimeError(
            f"CAT-20 has {len(users)} roster users but avatars.json has {len(portrait_ids)} portraits"
        )
    for user in users:
        pid = user.get("portrait") or user.get("id")
        if pid not in portrait_ids:
            raise RuntimeError(f"CAT-20 roster references unknown portrait {pid}")


def load_manifest() -> dict:
    if not MANIFEST_PATH.is_file():
        return {"schema": SCHEMA, "palettes": {}}
    data = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if data.get("schema") != SCHEMA:
        raise SystemExit(f"{MANIFEST_PATH} has unsupported schema {data.get('schema')}")
    data.setdefault("palettes", {})
    return data


def convert_one(
    source: Path,
    portrait_id: str,
    palettes: dict[str, list[str]],
    prior: dict | None,
    force: bool,
) -> tuple[dict | None, ConvertResult]:
    source_sha = _sha256_file(source)
    c_rel = portrait_c_asset(portrait_id)
    c_path = OUT_DIR / c_rel
    symbol = portrait_lvgl_symbol(portrait_id)

    if (
        not force
        and prior
        and prior.get("source_sha256") == source_sha
        and prior.get("c_asset") == c_rel
        and prior.get("symbol") == symbol
        and c_path.is_file()
    ):
        return prior, ConvertResult(portrait_id, source, True, "unchanged (skipped)", skipped=True)

    try:
        native = preprocess_native_rgba(source, portrait_id, palettes)
        with tempfile.TemporaryDirectory() as tmp:
            tmp_png = Path(tmp) / f"{portrait_id}.png"
            native.save(tmp_png)
            run_lvgl_convert(tmp_png, portrait_id, OUT_DIR)
            subprocess.run(
                [
                    sys.executable,
                    str(lvgl_image_path()),
                    "--cf",
                    "RGB565A8",
                    "--ofmt",
                    "BIN",
                    "--compress",
                    "NONE",
                    "--align",
                    "1",
                    "-o",
                    str(OUT_DIR),
                    str(tmp_png),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            bin_path = OUT_DIR / f"{portrait_id}.bin"
            if not bin_path.is_file():
                raise FileNotFoundError("LVGLImage BIN output missing")
            verify_rgb565a8_blob(native, bin_path.read_bytes())

        partial = sum(0 < p[3] < 255 for p in native.getdata())
        meta = {
            "source": str(source.relative_to(AVATARS_DOC)),
            "source_sha256": source_sha,
            "symbol": symbol,
            "c_asset": c_rel,
            "binary_asset": f"{portrait_id}.bin",
            "width": native.width,
            "height": native.height,
            "format": "LV_COLOR_FORMAT_RGB565A8",
            "partial_alpha_pixels": partial,
            "partial_alpha_percent": round(100 * partial / (native.width * native.height), 2),
            "raw_bytes": native.width * native.height * 3,
            "converted_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        }
        return meta, ConvertResult(portrait_id, source, True, "converted")
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        detail = exc.stderr if isinstance(exc, subprocess.CalledProcessError) and exc.stderr else str(exc)
        return None, ConvertResult(portrait_id, source, False, detail.strip() or str(exc))


def portrait_display_name(portrait_id: str) -> str:
    if portrait_id.startswith("avatar-") and len(portrait_id) > 7:
        tail = portrait_id[7:].replace("-", " ")
        return tail.upper() if len(tail) <= 4 else tail.title()
    return portrait_id.replace("-", " ").title()


def write_cat20_scene(portrait_ids: list[str]) -> None:
    """Regenerate LAB-14 bust roster scene: one card per packed portrait."""
    n = len(portrait_ids)
    focused = n // 2 if n else 0
    title = f"Portrait roster ({n})" if n else "Portrait roster (empty)"
    lines = [
        "id: CAT-20",
        "order: 20",
        f"title: {title}",
        "group: Login",
        "screen: SCR-SIGN-IN-ROSTER",
        "state: roster-ready-bust",
        "layout: LAYOUT-SPLIT-PANE",
        "notes: Auto-generated by make avatars; one COMP-USER-CARD-BUST per packed portrait",
        "slots:",
        "  upper:",
        "    panel: PANEL-HEADER-TITLE-LEFT",
        "    props:",
        f'      title: "{title}"',
        "  lower:",
        "    panel: PANEL-BODY-ROSTER-CAROUSEL",
        "    props:",
        f"      focused: {focused}",
        "      touch: true",
        "      card_style: bust",
        "      users:",
    ]
    for index, portrait_id in enumerate(portrait_ids):
        bg = CARD_BACKGROUNDS[index % len(CARD_BACKGROUNDS)]
        name = portrait_display_name(portrait_id)
        lines.append(
            f'        - {{id: {portrait_id}, name: "{name}", background: "{bg}", '
            f'text: "#101418", portrait: {portrait_id}}}'
        )
    CAT20_SCENE.parent.mkdir(parents=True, exist_ok=True)
    CAT20_SCENE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def prune_stale_outputs(active_ids: set[str]) -> list[str]:
    removed = []
    active_stems = {portrait_c_stem(pid) for pid in active_ids}
    for path in OUT_DIR.glob("portrait_*.c"):
        stem = path.stem.removeprefix("portrait_")
        canonical = portrait_c_stem(stem)
        if canonical not in active_stems:
            path.unlink()
            removed.append(path.name)
            continue
        if "-" in stem:
            path.unlink()
            removed.append(path.name)
    for path in OUT_DIR.glob("*.bin"):
        pid = path.stem
        if pid not in active_ids:
            path.unlink()
            removed.append(path.name)
    return removed


def discover_sources() -> list[Path]:
    if not AVATARS_DOC.is_dir():
        return []
    files = []
    for path in sorted(AVATARS_DOC.iterdir()):
        if not path.is_file():
            continue
        if path.name.startswith("."):
            continue
        if path.name in SKIP_SOURCE_NAMES:
            continue
        if path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        files.append(path)
    return files


def pack(force: bool = False) -> int:
    _require_pillow()
    try:
        import lz4  # noqa: F401
    except ImportError:
        print("warning: lz4 not installed; LVGLImage may fail. Run: pip install lz4", file=sys.stderr)
    lvgl_image_path()

    doc = load_manifest()
    palettes: dict[str, list[str]] = doc.get("palettes", {})
    prior_portraits: dict[str, dict] = doc.get("portraits", {})

    sources = discover_sources()
    portraits: dict[str, dict] = {}
    results: list[ConvertResult] = []

    for source in sources:
        try:
            portrait_id = portrait_id_from_name(source)
        except ValueError as exc:
            results.append(ConvertResult(source.stem, source, False, str(exc)))
            continue

        meta, result = convert_one(source, portrait_id, palettes, prior_portraits.get(portrait_id), force)
        results.append(result)
        if meta is not None:
            portraits[portrait_id] = meta

    removed = prune_stale_outputs(set(portraits))
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    write_registry({"portraits": portraits})

    out_doc = {
        "schema": SCHEMA,
        "updated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "palettes": palettes,
        "portraits": portraits,
    }
    MANIFEST_PATH.write_text(json.dumps(out_doc, indent=2) + "\n", encoding="utf-8")

    portrait_ids = sorted(portraits.keys())
    portrait_id_set = set(portrait_ids)
    write_cat20_scene(portrait_ids)
    pack_lab14_catalog()
    verify_cat20_catalog(portrait_id_set)

    ok = [r for r in results if r.ok and not r.skipped]
    skipped = [r for r in results if r.skipped]
    failed = [r for r in results if not r.ok]

    print(f"avatars: {len(portraits)} packed -> {OUT_DIR.relative_to(REPO_ROOT)}")
    print(f"catalog: CAT-20 updated ({len(portrait_ids)} bust cards) -> {CAT20_SCENE.relative_to(REPO_ROOT)}")
    for r in skipped:
        print(f"  ok (skip) {r.portrait_id} <- {r.source.name}")
    for r in ok:
        print(f"  ok         {r.portrait_id} <- {r.source.name}")
    for r in failed:
        print(f"  FAIL       {r.source.name}: {r.message}", file=sys.stderr)
    for name in removed:
        print(f"  removed stale {name}")

    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack profile avatar sources for firmware.")
    parser.add_argument("--force", action="store_true", help="Reconvert even when source SHA is unchanged.")
    args = parser.parse_args()
    return pack(force=args.force)


if __name__ == "__main__":
    raise SystemExit(main())
