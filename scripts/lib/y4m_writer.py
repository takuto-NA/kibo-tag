"""
Responsibility: build deterministic Y4M (I420) clips from JPEG tag fixtures for Chromium fake-camera.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

from PIL import Image

GENERATOR_VERSION = "1"
LUMA_BLACK = 16
LUMA_WHITE = 235
CHROMA_NEUTRAL = 128
BYTES_PER_RGBA_PIXEL = 4
I420_CHROMA_SUBSAMPLE = 2


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def rgba_to_i420(rgba_bytes: bytes, width: int, height: int) -> bytes:
    """Convert packed RGBA8 into planar I420 with BT.601-ish integer coefficients."""
    pixel_count = width * height
    y_plane = bytearray(pixel_count)
    chroma_width = width // I420_CHROMA_SUBSAMPLE
    chroma_height = height // I420_CHROMA_SUBSAMPLE
    u_plane = bytearray(chroma_width * chroma_height)
    v_plane = bytearray(chroma_width * chroma_height)

    for row in range(height):
        for column in range(width):
            pixel_index = row * width + column
            rgba_index = pixel_index * BYTES_PER_RGBA_PIXEL
            red = rgba_bytes[rgba_index]
            green = rgba_bytes[rgba_index + 1]
            blue = rgba_bytes[rgba_index + 2]
            # Integer BT.601 luma used by many capture paths; keep deterministic.
            luma = (66 * red + 129 * green + 25 * blue + 128) >> 8
            y_plane[pixel_index] = max(LUMA_BLACK, min(LUMA_WHITE, luma + 16))

            if row % I420_CHROMA_SUBSAMPLE == 0 and column % I420_CHROMA_SUBSAMPLE == 0:
                chroma_index = (row // I420_CHROMA_SUBSAMPLE) * chroma_width + (
                    column // I420_CHROMA_SUBSAMPLE
                )
                u_value = ((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128
                v_value = ((112 * red - 94 * green - 18 * blue + 128) >> 8) + 128
                u_plane[chroma_index] = max(0, min(255, u_value))
                v_plane[chroma_index] = max(0, min(255, v_value))

    return bytes(y_plane) + bytes(u_plane) + bytes(v_plane)


def load_tag_rgba(source_jpeg: Path, tag_pixel_size: int) -> Image.Image:
    tag_image = Image.open(source_jpeg).convert("RGBA")
    return tag_image.resize((tag_pixel_size, tag_pixel_size), Image.Resampling.NEAREST)


def compose_frame_rgba(
    frame_width: int,
    frame_height: int,
    tag_image: Image.Image,
    center_x: int,
    center_y: int,
    background_luma: int,
) -> bytes:
    background = Image.new(
        "RGBA",
        (frame_width, frame_height),
        (background_luma, background_luma, background_luma, 255),
    )
    left = center_x - tag_image.width // 2
    top = center_y - tag_image.height // 2
    background.paste(tag_image, (left, top), tag_image)
    return background.tobytes()


def write_y4m(
    output_path: Path,
    frames_i420: list[bytes],
    frame_width: int,
    frame_height: int,
    frames_per_second: int,
) -> None:
    header = (
        f"YUV4MPEG2 W{frame_width} H{frame_height} "
        f"F{frames_per_second}:1 Ip A1:1 C420jpeg XYSCSS=420JPEG\n"
    ).encode("ascii")
    frame_header = b"FRAME\n"
    with output_path.open("wb") as handle:
        handle.write(header)
        for frame_bytes in frames_i420:
            handle.write(frame_header)
            handle.write(frame_bytes)


def expected_corners(center_x: int, center_y: int, tag_pixel_size: int) -> list[list[float]]:
    half = tag_pixel_size / 2.0
    return [
        [center_x - half, center_y - half],
        [center_x + half, center_y - half],
        [center_x + half, center_y + half],
        [center_x - half, center_y + half],
    ]


def build_clip(args: argparse.Namespace) -> dict:
    source_jpeg = Path(args.source_jpeg).resolve() if args.source_jpeg else None
    output_y4m = Path(args.output_y4m).resolve()
    output_manifest = Path(args.output_manifest).resolve()
    output_y4m.parent.mkdir(parents=True, exist_ok=True)

    tag_image = None
    if args.scene != "empty":
        # Guard: non-empty scenes require a JPEG source for the tag bitmap.
        if source_jpeg is None:
            raise ValueError("source_jpeg is required unless scene=empty")
        tag_image = load_tag_rgba(source_jpeg, args.tag_pixel_size)

    frames_i420: list[bytes] = []
    corner_trajectory: list[dict] = []
    static_frame_i420: bytes | None = None

    for frame_index in range(args.frame_count):
        if args.motion == "static":
            center_x = args.center_x
            center_y = args.center_y
        elif args.motion == "horizontal":
            amplitude = args.motion_amplitude_px
            phase = (2.0 * math.pi * frame_index) / max(args.frame_count, 1)
            center_x = args.center_x + int(round(amplitude * math.sin(phase)))
            center_y = args.center_y
        else:
            raise ValueError(f"Unsupported motion mode: {args.motion}")

        can_reuse_static_frame = args.motion == "static" and static_frame_i420 is not None
        if can_reuse_static_frame:
            frames_i420.append(static_frame_i420)
            corners = (
                []
                if args.scene == "empty"
                else expected_corners(center_x, center_y, args.tag_pixel_size)
            )
        else:
            if args.scene == "empty":
                rgba = compose_frame_rgba(
                    args.frame_width,
                    args.frame_height,
                    Image.new("RGBA", (1, 1), (0, 0, 0, 0)),
                    center_x,
                    center_y,
                    args.background_luma,
                )
                corners = []
            else:
                rgba = compose_frame_rgba(
                    args.frame_width,
                    args.frame_height,
                    tag_image,
                    center_x,
                    center_y,
                    args.background_luma,
                )
                corners = expected_corners(center_x, center_y, args.tag_pixel_size)

            frame_i420 = rgba_to_i420(rgba, args.frame_width, args.frame_height)
            if args.motion == "static":
                static_frame_i420 = frame_i420
            frames_i420.append(frame_i420)

        media_timestamp_seconds = frame_index / float(args.frames_per_second)
        corner_trajectory.append(
            {
                "frameIndex": frame_index,
                "mediaTimestampSeconds": media_timestamp_seconds,
                "center": [center_x, center_y],
                "corners": corners,
                "expectedTagIds": args.expected_tag_ids,
                "expectedFamily": args.expected_family,
            }
        )

    write_y4m(
        output_y4m,
        frames_i420,
        args.frame_width,
        args.frame_height,
        args.frames_per_second,
    )

    manifest = {
        "generatorVersion": GENERATOR_VERSION,
        "seed": args.seed,
        "scene": args.scene,
        "sourceJpeg": str(source_jpeg.as_posix()) if source_jpeg is not None else None,
        "sourceJpegSha256": sha256_file(source_jpeg) if source_jpeg is not None else None,
        "y4mPath": str(output_y4m.as_posix()),
        "y4mSha256": sha256_file(output_y4m),
        "frameWidth": args.frame_width,
        "frameHeight": args.frame_height,
        "frameCount": args.frame_count,
        "framesPerSecond": args.frames_per_second,
        "motion": args.motion,
        "tagPixelSize": args.tag_pixel_size,
        "backgroundLuma": args.background_luma,
        "blurModel": {
            "kind": "none",
            "kernelPx": 0,
        },
        "expectedFamily": args.expected_family,
        "expectedTagIds": args.expected_tag_ids,
        "cornerTrajectory": corner_trajectory,
    }
    output_manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-jpeg", default="")
    parser.add_argument("--output-y4m", required=True)
    parser.add_argument("--output-manifest", required=True)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--scene", choices=("tag", "empty"), default="tag")
    parser.add_argument("--frame-width", type=int, default=1280)
    parser.add_argument("--frame-height", type=int, default=720)
    parser.add_argument("--frame-count", type=int, default=90)
    parser.add_argument("--frames-per-second", type=int, default=30)
    parser.add_argument("--tag-pixel-size", type=int, default=240)
    parser.add_argument("--center-x", type=int, default=640)
    parser.add_argument("--center-y", type=int, default=360)
    parser.add_argument("--background-luma", type=int, default=200)
    parser.add_argument("--motion", choices=("static", "horizontal"), default="static")
    parser.add_argument("--motion-amplitude-px", type=int, default=80)
    parser.add_argument("--expected-family", default="tag36h11")
    parser.add_argument("--expected-tag-ids", default="0")
    args = parser.parse_args(argv)
    args.source_jpeg = args.source_jpeg or None
    args.expected_tag_ids = [int(part) for part in args.expected_tag_ids.split(",") if part != ""]
    return args


def main(argv: list[str]) -> int:
    manifest = build_clip(parse_args(argv))
    sys.stdout.write(json.dumps({"y4mSha256": manifest["y4mSha256"]}, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
