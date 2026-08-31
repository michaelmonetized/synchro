#!/usr/bin/env python3
"""Measure opt-in vision-language metadata enrichment through local Ollama."""

from __future__ import annotations

import argparse
import base64
from io import BytesIO
import json
from pathlib import Path
import statistics
import time
from urllib.request import Request, urlopen

from PIL import Image


PROMPT = """Analyze this local file preview for a desktop file catalog.
Return one compact JSON object with these keys:
category, summary, objects, scene, visual_style, dominant_colors,
searchable_phrases, contains_text, and confidence.
Use short factual values. Do not infer identity or sensitive traits.
searchable_phrases should contain 5 to 10 phrases a person might type to find it.
"""


def encoded_preview(path: Path) -> str:
    with Image.open(path) as source:
        image = source.convert("RGB")
        image.thumbnail((768, 768), Image.Resampling.LANCZOS)
    buffer = BytesIO()
    image.save(buffer, "JPEG", quality=85, optimize=True)
    return base64.b64encode(buffer.getvalue()).decode()


def enrich(path: Path, model: str) -> dict:
    request = Request(
        "http://127.0.0.1:11434/api/chat",
        data=json.dumps(
            {
                "model": model,
                "stream": False,
                "format": "json",
                "options": {"temperature": 0},
                "messages": [
                    {"role": "user", "content": PROMPT, "images": [encoded_preview(path)]}
                ],
            }
        ).encode(),
        headers={"Content-Type": "application/json"},
    )
    started = time.perf_counter()
    with urlopen(request, timeout=600) as response:
        payload = json.load(response)
    elapsed = time.perf_counter() - started
    content = payload["message"]["content"]
    try:
        metadata = json.loads(content)
    except json.JSONDecodeError:
        metadata = {"raw": content, "validJson": False}
    return {
        "path": str(path),
        "elapsedSeconds": round(elapsed, 3),
        "loadSeconds": round(payload.get("load_duration", 0) / 1e9, 3),
        "promptTokens": payload.get("prompt_eval_count"),
        "outputTokens": payload.get("eval_count"),
        "metadata": metadata,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", default="gemma3:4b")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    results = [enrich(path, args.model) for path in args.paths]
    payload = {
        "model": args.model,
        "images": len(results),
        "medianSeconds": round(statistics.median(row["elapsedSeconds"] for row in results), 3),
        "results": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps(payload, indent=2))


if __name__ == "__main__":
    main()
