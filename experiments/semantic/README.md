# Semantic retrieval experiment

This bounded benchmark compares a CPU-only BGE-small baseline with
EmbeddingGemma served by the local Ollama runtime. Input is a JSON result from
Synchro's indexed catalog; the script does not discover files itself.

```sh
python benchmark_text.py \
  --manifest /tmp/synchro-semantic-research/text-corpus.json \
  --backend bge \
  --output /tmp/synchro-semantic-research/results/bge.json

python benchmark_text.py \
  --manifest /tmp/synchro-semantic-research/text-corpus.json \
  --backend embeddinggemma \
  --dimensions 256 \
  --output /tmp/synchro-semantic-research/results/embeddinggemma.json
```

The quality set is intentionally small and Synchro-specific. It is useful for
comparing candidate models and chunking choices, not as a general model claim.
Embeddings are generated in fixed micro-batches so the measurement reflects a
background queue rather than an unbounded one-shot allocation.

The image smoke test uses matching CLIP text and vision encoders and produces a
contact sheet for human inspection:

```sh
python benchmark_images.py \
  --manifest /tmp/synchro-semantic-research/image-corpus.json \
  --output /tmp/synchro-semantic-research/results/clip.json \
  --contact-sheet /tmp/synchro-semantic-research/results/clip-contact.jpg
```

Deep metadata is a separate opt-in experiment. The current harness uses a
locally served vision-language model and records cold-load and per-image time:

```sh
python benchmark_enrichment.py \
  --output /tmp/synchro-semantic-research/results/enrichment.json \
  image-one.jpg image-two.png
```
