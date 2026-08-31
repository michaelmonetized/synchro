#!/usr/bin/env python3
"""Small, reproducible semantic-retrieval benchmark for Synchro experiments."""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import statistics
import time
from typing import Callable, Iterable
from urllib.request import Request, urlopen

import numpy as np
import psutil
from fastembed import TextEmbedding


QUESTIONS = [
    ("thumbnail generation, cache lookup, and visible viewport priority", {"ThumbnailService.cpp", "ThumbCache.cpp"}),
    ("persistent SQL file catalog and DuckDB shadow rebuild", {"FileCatalog.cpp"}),
    ("three dimensional spatial layout for MapV and StrataV", {"FsnLayout.cpp", "FileFsn.qml"}),
    ("save and open file chooser portal behavior", {"PortalService.cpp", "ChooserWindow.qml"}),
    ("sortable grid keyboard navigation using rendered item geometry", {"FileGrid.qml", "FileGridCell.qml"}),
    ("preview a DuckDB or Parquet database file", {"DbPreview.cpp", "ParquetMeta.cpp", "DatabasePeek.qml"}),
    ("execute contextual file actions and load handlers", {"HandlerActions.cpp", "HandlerLoader.cpp", "HandlerExec.cpp"}),
    ("content search with ripgrep and result cancellation", {"SearchService.cpp", "SearchModel.cpp"}),
]


def load_documents(manifest_path: Path, max_chars: int) -> list[dict]:
    payload = json.loads(manifest_path.read_text())
    documents = []
    excluded_parts = {".git", "build", "build-dogfood", "CMakeFiles", "node_modules"}
    for row in payload["rows"]:
        path = Path(row["path"])
        if any(part in excluded_parts for part in path.parts) or not path.is_file():
            continue
        try:
            body = path.read_text(errors="replace")[:max_chars]
        except OSError:
            continue
        if not body.strip():
            continue
        documents.append(
            {
                "path": str(path),
                "name": path.name,
                "text": f"file: {path.name}\npath: {path}\n\n{body}",
            }
        )
    return documents


def chunks(document: dict, chunk_chars: int, overlap: int) -> Iterable[dict]:
    text = document["text"]
    step = max(1, chunk_chars - overlap)
    for index, start in enumerate(range(0, len(text), step)):
        body = text[start : start + chunk_chars]
        if body.strip():
            yield {**document, "chunk": index, "text": body}
        if start + chunk_chars >= len(text):
            break


def normalize(vectors: np.ndarray) -> np.ndarray:
    norms = np.linalg.norm(vectors, axis=1, keepdims=True)
    return vectors / np.maximum(norms, 1e-12)


def ollama_embed(texts: list[str], model: str, dimensions: int | None = None) -> np.ndarray:
    request = Request(
        "http://127.0.0.1:11434/api/embed",
        data=json.dumps({"model": model, "input": texts, "truncate": True}).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urlopen(request, timeout=600) as response:
        vectors = np.asarray(json.load(response)["embeddings"], dtype=np.float32)
    if dimensions and dimensions < vectors.shape[1]:
        vectors = vectors[:, :dimensions]
    return normalize(vectors)


def fastembed_factory(model_name: str) -> tuple[Callable[[list[str], bool], np.ndarray], float]:
    started = time.perf_counter()
    model = TextEmbedding(model_name=model_name, providers=["CPUExecutionProvider"], threads=8)
    load_seconds = time.perf_counter() - started

    def embed(texts: list[str], query: bool) -> np.ndarray:
        iterator = model.query_embed(texts) if query else model.passage_embed(texts)
        return normalize(np.asarray(list(iterator), dtype=np.float32))

    return embed, load_seconds


def ollama_factory(model_name: str, dimensions: int | None) -> tuple[Callable[[list[str], bool], np.ndarray], float]:
    started = time.perf_counter()
    ollama_embed(["warmup"], model_name, dimensions)
    load_seconds = time.perf_counter() - started
    return lambda texts, query: ollama_embed(texts, model_name, dimensions), load_seconds


def evaluate(chunk_rows: list[dict], document_vectors: np.ndarray, query_vectors: np.ndarray) -> dict:
    rankings = []
    recalls = {1: 0, 5: 0, 10: 0}
    reciprocal_ranks = []
    for question_index, (question, expected) in enumerate(QUESTIONS):
        similarities = document_vectors @ query_vectors[question_index]
        best_by_path: dict[str, float] = {}
        for row, score in zip(chunk_rows, similarities, strict=True):
            best_by_path[row["path"]] = max(float(score), best_by_path.get(row["path"], -math.inf))
        ranked_paths = sorted(best_by_path, key=best_by_path.get, reverse=True)
        ranked_names = [Path(path).name for path in ranked_paths]
        rank = next((i + 1 for i, name in enumerate(ranked_names) if name in expected), None)
        reciprocal_ranks.append(0.0 if rank is None else 1.0 / rank)
        for cutoff in recalls:
            recalls[cutoff] += int(any(name in expected for name in ranked_names[:cutoff]))
        rankings.append(
            {
                "question": question,
                "expected": sorted(expected),
                "firstRelevantRank": rank,
                "top5": [
                    {"name": Path(path).name, "path": path, "score": round(best_by_path[path], 4)}
                    for path in ranked_paths[:5]
                ],
            }
        )
    total = len(QUESTIONS)
    return {
        "recallAt1": recalls[1] / total,
        "recallAt5": recalls[5] / total,
        "recallAt10": recalls[10] / total,
        "mrr": statistics.mean(reciprocal_ranks),
        "rankings": rankings,
    }


def percentile(samples: list[float], fraction: float) -> float:
    ordered = sorted(samples)
    return ordered[min(len(ordered) - 1, round((len(ordered) - 1) * fraction))]


def embed_batched(
    embed: Callable[[list[str], bool], np.ndarray],
    texts: list[str],
    query: bool,
    batch_size: int,
) -> np.ndarray:
    vectors = []
    for start in range(0, len(texts), batch_size):
        vectors.append(embed(texts[start : start + batch_size], query))
    return np.concatenate(vectors, axis=0)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--backend", choices=("bge", "embeddinggemma"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-chars", type=int, default=12000)
    parser.add_argument("--chunk-chars", type=int, default=3200)
    parser.add_argument("--overlap", type=int, default=300)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--dimensions", type=int, default=256)
    args = parser.parse_args()

    documents = load_documents(args.manifest, args.max_chars)
    chunk_rows = [chunk for document in documents for chunk in chunks(document, args.chunk_chars, args.overlap)]
    if args.backend == "bge":
        embed, load_seconds = fastembed_factory("BAAI/bge-small-en-v1.5")
        model_name = "BAAI/bge-small-en-v1.5"
    else:
        embed, load_seconds = ollama_factory("embeddinggemma:300m", args.dimensions)
        model_name = "embeddinggemma:300m"

    before_rss = psutil.Process(os.getpid()).memory_info().rss
    started = time.perf_counter()
    document_vectors = embed_batched(
        embed,
        [row["text"] for row in chunk_rows],
        False,
        args.batch_size,
    )
    document_seconds = time.perf_counter() - started
    after_rss = psutil.Process(os.getpid()).memory_info().rss

    latency_ms = []
    query_vectors = []
    for question, _ in QUESTIONS:
        started = time.perf_counter()
        query_vectors.append(embed([question], True)[0])
        latency_ms.append((time.perf_counter() - started) * 1000)
    query_matrix = np.asarray(query_vectors, dtype=np.float32)

    result = {
        "backend": args.backend,
        "model": model_name,
        "documents": len(documents),
        "chunks": len(chunk_rows),
        "dimensions": int(document_vectors.shape[1]),
        "loadSeconds": round(load_seconds, 4),
        "indexSeconds": round(document_seconds, 4),
        "chunksPerSecond": round(len(chunk_rows) / document_seconds, 2),
        "queryP50Ms": round(percentile(latency_ms, 0.5), 3),
        "queryP95Ms": round(percentile(latency_ms, 0.95), 3),
        "processRssDeltaMiB": round((after_rss - before_rss) / 1024 / 1024, 2),
        "float32VectorMiBPerMillionChunks": round(document_vectors.shape[1] * 4_000_000 / 1024 / 1024, 2),
        "quality": evaluate(chunk_rows, document_vectors, query_matrix),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "quality"}, indent=2))
    print(json.dumps({key: value for key, value in result["quality"].items() if key != "rankings"}, indent=2))


if __name__ == "__main__":
    main()
