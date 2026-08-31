#!/usr/bin/env python3
"""CLIP image-search smoke benchmark over a Synchro catalog manifest."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFont
import psutil
from fastembed import ImageEmbedding, TextEmbedding


QUERIES = [
    "a screenshot of desktop software or a user interface",
    "a portrait or photograph of a person",
    "a science fiction illustration or cinematic wallpaper",
    "a simple logo, symbol, or app icon",
    "a predominantly blue image",
    "a very dark image",
]


def normalized(vectors) -> np.ndarray:
    values = np.asarray(list(vectors), dtype=np.float32)
    return values / np.maximum(np.linalg.norm(values, axis=1, keepdims=True), 1e-12)


def load_rows(manifest: Path, limit: int) -> tuple[list[dict], list[Image.Image]]:
    rows = []
    images = []
    for row in json.loads(manifest.read_text())["rows"]:
        if len(rows) >= limit:
            break
        try:
            with Image.open(row["path"]) as source:
                image = source.convert("RGB")
                image.thumbnail((1024, 1024), Image.Resampling.LANCZOS)
                images.append(image.copy())
                rows.append(row)
        except (OSError, ValueError):
            continue
    return rows, images


def make_contact_sheet(rows: list[dict], rankings: list[list[int]], output: Path) -> None:
    tile_w, tile_h, label_h = 180, 125, 38
    columns = 8
    canvas = Image.new("RGB", (columns * tile_w, len(rankings) * (tile_h + label_h + 28)), "#0b0e17")
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()
    for query_index, indices in enumerate(rankings):
        top = query_index * (tile_h + label_h + 28)
        draw.text((8, top + 6), QUERIES[query_index], fill="#e9d8cf", font=font)
        for column, row_index in enumerate(indices[:columns]):
            path = Path(rows[row_index]["path"])
            try:
                with Image.open(path) as source:
                    image = source.convert("RGB")
                    image.thumbnail((tile_w - 8, tile_h - 8), Image.Resampling.LANCZOS)
            except OSError:
                continue
            x = column * tile_w + (tile_w - image.width) // 2
            y = top + 28 + (tile_h - image.height) // 2
            canvas.paste(image, (x, y))
            name = path.name[:24]
            draw.text((column * tile_w + 5, top + 28 + tile_h + 3), name, fill="#aaa4ae", font=font)
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--contact-sheet", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=96)
    parser.add_argument("--batch-size", type=int, default=8)
    args = parser.parse_args()

    rows, images = load_rows(args.manifest, args.limit)
    started = time.perf_counter()
    vision = ImageEmbedding(model_name="Qdrant/clip-ViT-B-32-vision", providers=["CPUExecutionProvider"])
    text = TextEmbedding(model_name="Qdrant/clip-ViT-B-32-text", providers=["CPUExecutionProvider"])
    load_seconds = time.perf_counter() - started

    before_rss = psutil.Process(os.getpid()).memory_info().rss
    batches = []
    started = time.perf_counter()
    for offset in range(0, len(images), args.batch_size):
        batches.append(normalized(vision.embed(images[offset : offset + args.batch_size])))
    image_vectors = np.concatenate(batches, axis=0)
    image_seconds = time.perf_counter() - started
    after_rss = psutil.Process(os.getpid()).memory_info().rss

    started = time.perf_counter()
    query_vectors = normalized(text.embed(QUERIES))
    query_seconds = time.perf_counter() - started
    scores = query_vectors @ image_vectors.T
    rankings = [list(np.argsort(row)[::-1]) for row in scores]
    make_contact_sheet(rows, rankings, args.contact_sheet)

    ranked_results = []
    for query, ranking, query_scores in zip(QUERIES, rankings, scores, strict=True):
        ranked_results.append(
            {
                "query": query,
                "top8": [
                    {
                        "path": rows[index]["path"],
                        "score": round(float(query_scores[index]), 4),
                        "colorFamily": rows[index].get("color_family"),
                        "brightness": rows[index].get("brightness"),
                        "saturation": rows[index].get("saturation"),
                    }
                    for index in ranking[:8]
                ],
            }
        )

    result = {
        "model": "CLIP ViT-B/32 ONNX",
        "images": len(images),
        "dimensions": int(image_vectors.shape[1]),
        "loadSeconds": round(load_seconds, 4),
        "embedSeconds": round(image_seconds, 4),
        "imagesPerSecond": round(len(images) / image_seconds, 2),
        "queryBatchMs": round(query_seconds * 1000, 3),
        "processRssDeltaMiB": round((after_rss - before_rss) / 1024 / 1024, 2),
        "float32VectorMiBPerMillionImages": round(image_vectors.shape[1] * 4_000_000 / 1024 / 1024, 2),
        "results": ranked_results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "results"}, indent=2))


if __name__ == "__main__":
    main()
