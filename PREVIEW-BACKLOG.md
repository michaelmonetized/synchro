# Preview / thumbnail backlog

Daily-drive list. Peek = Space overlay. Thumb = listing + folder mosaic tile.

## Core (do these first)

- [ ] **Folder mosaic follows listing order** and stays readable for text/markdown (not four black stamps).
- [ ] **Markdown thumbs** stay paper cards; mosaic uses the same first-four name-sorted files you see at the top of the folder.
- [ ] **Office / docs:** `.docx` / `.odt` peek + thumb (LibreOffice thumbnailer already on many boxes; peek via convert-to-pdf or first-page raster).
- [ ] **Spreadsheets:** `.xlsx` / `.ods` / `.csv` peek (csv already text; sheets need a first-sheet raster or html dump).
- [ ] **Audio:** `.mp3` / `.flac` / `.ogg` / `.wav` — cover art thumb if tagged, else a waveform/card; peek = muted player or metadata.
- [x] **Archives:** `.zip` / `.tar` / `.gz` / `.tgz` peek as a member list (not a fake folder). `.7z` still later.
- [ ] **Code:** highlight a few more suffixes in the text card (`.tsx`, `.vue`, `.rb`, `.java`, `.kt`) — peek already falls through to text if matched.

## Next

- [ ] **EPUB / comics:** `.epub` cover, `.cbz` first page.
- [ ] **Fonts:** `.ttf` / `.otf` specimen card.
- [ ] **SVG:** peek as image (Qt SVG); thumb via raster.
- [ ] **HEIC / RAW:** already have some decode fallbacks; verify camera dumps and add a raw card if decode fails.
- [ ] **HTML:** sandboxed text/metadata peek (do not execute); thumb = title + first text.
- [ ] **Desktop / Omarchy launchers:** `.desktop` peek shows name/exec; thumb = icon from theme.
- [ ] **Git repo folder mosaic:** optional later — not a handler, a folder-thumb hint.

## Not yet

- Semantic / AI descriptions on thumbs.
- Recursive folder mosaics (immediate children only).
- Treating zip as a navigable folder (`folder` handler).

## How to add one

1. `handlers/synchro.preview.<kind>/{manifest.json,Preview.qml}` — entry QML can be named for the type (`sqlite.qml`, `duckdb.qml`).
2. `kinds: ["preview"]` and optionally `"thumbnail": { "runtime": "core", "verb": "text-card" }` or a system `.thumbnailer`
3. Match on mime + suffix. First-party previews must not execute file content.
4. Interactive peeks: implement `peekKey(key, modifiers)` on the `HandlerSurface` (return true if consumed) and/or set `peekFlickable`. The list keeps Qt focus. `A`/`D` hop index ↔ file; `W`/`S` then go to `peekKey`. Host helpers: `readPreview`, `readParquet`, `readDatabase(...)`, `readArchive(file, maxEntries)`. Shared chrome: `DatabasePeek`, `ArchivePeek`.
5. Test: peek opens, listing thumb appears, folder mosaic includes it if it is in the first four name-sorted children.
