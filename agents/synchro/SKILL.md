---
name: synchro
description: Use Synchro's local indexed file catalog, current browser selection, deterministic metadata, saved organization, and navigable SQL result folders. Use for finding, comparing, grouping, or inspecting local files and projects on Omarchy.
---

# Synchro file catalog

Synchro exposes a stable CLI contract that works with Omarchy's configured
agent without MCP or agent-specific configuration.

## Start with context

When launched from Synchro, run this first:

```bash
"${SYNCHRO_BIN:-synchro}" agent context --compact
```

`SYNCHRO_BIN` is the exact running binary and supports uninstalled development
builds; an ordinary installed launch falls back to `synchro`. The context
command reads `SYNCHRO_SELECTION` and `SYNCHRO_CWD` from the handoff environment and
returns the explicit selection, working folder, available relations, fields,
examples, and result-safety rules as JSON. Treat that document as authoritative.

## Query before walking

Prefer the durable catalog for discovery and deterministic metadata:

```bash
"${SYNCHRO_BIN:-synchro}" agent query --cwd "$SYNCHRO_CWD" --sql \
  "select name,path,extension,kind,mb from tree where not is_dir and extension in ('jpg','jpeg','png','gif','webp','avif','bmp','tif','tiff','heic','heif','svg') order by size desc limit 20"

"${SYNCHRO_BIN:-synchro}" agent query --cwd "$SYNCHRO_CWD" --sql \
  "select * from selection order by path"
```

The useful relations are `files`, `here`, `tree`, `selection`, `facts`,
`image_facts`, and `projects`. Only `SELECT` and `WITH` are accepted. Output is
JSON. Check `catalog.coverageComplete` and `truncated` before describing a
result as exhaustive.

The context document includes every relation's fields; do not probe schemas
with `select *` first. On multi-million-row catalogs, filter images by
`extension` before sorting because `kind` is derived. Use `tree` for the
recursive cwd scope, `here` for one directory, `selection` for explicit items,
and `files` only when a deliberately catalog-wide query is requested. During
an agent handoff, use `agent query`, not the lower-level `query`: its narrow
Omarchy user-service broker reads only through Synchro's validated query
surface without granting the agent broad access to the catalog directory.

To hand a useful result back as a navigable Synchro pseudo-folder:

```bash
"${SYNCHRO_BIN:-synchro}" agent show --cwd "$SYNCHRO_CWD" --label "large images" --sql \
  "select name,path,extension,kind,mb from tree where not is_dir and extension in ('jpg','jpeg','png','gif','webp','avif','bmp','tif','tiff','heic','heif','svg') order by size desc limit 20"
```

Use filesystem reads only when the indexed metadata is insufficient. The
optional `synchro mcp --stdio` surface offers typed conveniences, but never
assume it is registered or require it for ordinary work.
