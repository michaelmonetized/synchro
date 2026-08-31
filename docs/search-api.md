# Synchro Search1 API

`org.omarchy.Synchro.Search1` is Synchro's versioned, per-user search service.
It is owned by `synchro-indexd.service`, so consumers do not need to launch the
file browser or open the catalog directly. The same surface is intended for
Omarchy shell plugins, launchers, agents, file pickers, and compatibility
adapters.

## CLI

```bash
# Fast indexed filename search. Terms are ANDed.
synchro search "test pdf" --kind file --extension pdf --limit 20 --compact

# Restrict results recursively to a location.
synchro search project --cwd "$HOME/repos" --scope cwd --compact

# Existing bounded literal content search through the same contract.
synchro search "import re" --content --cwd "$HOME/repos" --compact

synchro search describe --compact
synchro search status --cwd "$HOME" --compact
```

The command prefers D-Bus and reports `"transport":"dbus"`. Source-tree and
recovery use can fall back to the same implementation in-process and report
`"transport":"direct-fallback"`.

The existing `synchro query` command now uses `StartQuery` as well, so shell,
agent, launcher, and future desktop clients share the same bounded daemon query
path instead of opening the catalog independently.

## D-Bus

- Service/interface: `org.omarchy.Synchro.Search1`
- Object: `/org/omarchy/Synchro/Search1`
- API version: `1`

```bash
busctl --user introspect org.omarchy.Synchro.Search1 \
  /org/omarchy/Synchro/Search1
```

Methods:

| Method | Inputs | Output |
| --- | --- | --- |
| `Describe` | none | capabilities map |
| `Status` | options map | service/catalog/shadow status |
| `StartSearch` | query, options | request ID |
| `StartQuery` | read-only SQL, options | request ID |
| `Result` | request ID | running or terminal result map |
| `Cancel` | request ID | whether a running request was canceled |
| `Open` | path/URI, options | launch receipt |
| `Reveal` | path/URI, options | Synchro launch receipt |
| `Show` | read-only SQL, options | SQL pseudo-folder launch receipt |

`ResultsReady(requestId, result)` is emitted for signal-driven clients. Polling
`Result` is also supported. Terminal states are `ready`, `failed`, and
`canceled`; completed requests remain readable for 60 seconds.

### Search options

| Key | Meaning |
| --- | --- |
| `cwd` | ranking context and root for scoped searches |
| `mode` | `name` (default) or `content` |
| `limit` | 1-100 rows |
| `offset` | stable offset into the bounded ranked result |
| `scope` | `all` or recursive `cwd` |
| `kind` / `kinds` | `file`, `folder`, or `saved-query` |
| `extension` / `extensions` | suffixes with or without a leading dot |
| `includeHidden` | include dotfiles and results beneath hidden path components |
| `minSize` / `maxSize` | byte bounds |
| `modifiedAfter` / `modifiedBefore` | epoch-millisecond bounds |
| `timeoutMs` | bounded content-search deadline |

Each file result includes a stable ID, path, file URI, name, parent, kind,
extension, size, mtime, score, bookmark state, and a cached thumbnail URL when
one exists. The response's `catalog` map identifies the catalog revision,
coverage, search-index progress, and change sequence used for the result.
Scoped name searches constrain exact, prefix, and trigram candidate selection
before ranking, so common names elsewhere in an 11-million-row catalog cannot
crowd local matches out of the result window.
`candidateLimit` and `candidateWindowFull` make the bounded ranking window
explicit. `hasMore` only promises another page inside the materialized window;
clients are never sent into an empty pagination loop merely because additional
lower-ranked catalog candidates may exist.

## Performance and cancellation contract

Name search uses Synchro's incremental SQLite FTS/name index. Read-only SQL uses
the active DuckDB generation where available. Both execute outside the daemon's
event loop in a bounded two-worker pool.

Cancellation immediately makes a request terminal and suppresses stale result
publication. A library operation already running in a worker may finish in the
background; its result is discarded. This keeps clients responsive without
unsafe thread termination.

Content search is currently bounded live `ripgrep`, not a persisted content
index. `Describe` reports this explicitly as `contentIndexed:false`, leaving a
future content/semantic index free to improve the implementation without
changing the API.

## Optional visual-description search

The semantic image sidecar is intentionally separate from Search1 and the
load-bearing filename catalog. When enabled in Indexer Settings, it is exposed
through a stable local CLI:

```bash
synchro semantic search "blue industrial buildings" --cwd "$HOME/Pictures" \
  --limit 60 --compact
synchro semantic status --compact
```

The command compares the matching local CLIP text vector against normalized
image vectors beneath `cwd` and returns ordinary file rows with `similarity`,
`searched`, and `truncated` metadata. It does no filesystem discovery. The GUI
command `:see <description>` runs this asynchronously and renders the response
in the current window as a previewable result folder.

`Open`, `Reveal`, and `Show` currently launch an application or a new Synchro
window. Their receipts report `currentWindow:false` where relevant. A future
browser-instance routing layer can improve that behavior without changing the
search contract.
