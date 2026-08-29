import Quickshell
import Quickshell.Io
import QtQuick

Item {
  id: root

  property string binary: Quickshell.env("SYNCHRO_BIN") || "synchro"
  property string cwd: Quickshell.env("HOME") || "/"
  property string query: ""
  property bool explicitMode: false
  property bool contentMode: false
  property bool busy: false
  property string error: ""
  property var rows: []
  property var indexStatus: ({})
  property int limit: explicitMode ? 8 : 6

  property string pendingQuery: ""
  property bool pendingExplicit: false
  property bool pendingContent: false
  property int requestSerial: 0

  signal changed()

  function schedule(rawText) {
    var raw = String(rawText || "").trim()
    var isContent = raw.indexOf("//") === 0
    var isExplicit = !isContent && raw.charAt(0) === "/"
    var nextQuery = (isContent ? raw.substring(2)
                               : (isExplicit ? raw.substring(1) : raw)).trim()
    var minimum = isContent ? 2 : (isExplicit ? 1 : 3)

    requestSerial += 1
    pendingQuery = nextQuery
    pendingExplicit = isExplicit
    pendingContent = isContent
    debounce.stop()
    if (searchProc.running) searchProc.running = false

    if (nextQuery.length < minimum) {
      query = ""
      explicitMode = isExplicit
      contentMode = isContent
      busy = false
      error = ""
      rows = []
      indexStatus = ({})
      changed()
      return
    }

    explicitMode = isExplicit
    contentMode = isContent
    busy = true
    error = ""
    rows = []
    changed()
    debounce.restart()
  }

  function cancel() {
    requestSerial += 1
    debounce.stop()
    if (searchProc.running) searchProc.running = false
    query = ""
    contentMode = false
    busy = false
    error = ""
    rows = []
    indexStatus = ({})
    changed()
  }

  function activate(item, execute) {
    if (!item) return false
    var kind = String(item.kind || "")
    if (kind === "saved-query") {
      Quickshell.execDetached([
        binary,
        "--sql-query", String(item.sql || ""),
        "--sql-cwd", String(item.cwd || cwd),
        "--sql-label", String(item.name || "Saved query")
      ])
      return true
    }

    var path = String(item.path || "")
    if (!path) return false
    if (execute) {
      Quickshell.execDetached(["uwsm-app", "--", "xdg-open", path])
    } else if (kind === "folder") {
      Quickshell.execDetached([binary, "--new-window", path])
    } else {
      Quickshell.execDetached([binary, "--new-window", "--select", path])
    }
    return true
  }

  function iconFor(item) {
    if (item.kind === "saved-query") return "󰆼"
    if (item.matchType === "content") return "󰱼"
    if (item.kind === "folder") return item.bookmarked ? "󰉐" : "󰉋"
    return item.bookmarked ? "󰈙" : "󰈔"
  }

  function detailFor(item) {
    if (item.kind === "saved-query")
      return "saved query  ·  " + String(item.cwd || cwd)
    var parent = String(item.parent || "")
    if (item.matchType === "content")
      return "content match  ·  " + parent
    return (item.kind === "folder" ? "folder  ·  " : "file  ·  ") + parent
  }

  function sectionLabel() {
    if (contentMode) return "FILE CONTENT  ·  ↵ LOCATE  ·  ^↵ OPEN"
    if (indexStatus.complete === true)
      return "FILES & FOLDERS  ·  ↵ LOCATE  ·  ^↵ OPEN"
    var indexed = Number(indexStatus.indexedThroughRowid || 0)
    var target = Number(indexStatus.targetRowid || 0)
    if (indexed > 0 && target > 0)
      return "FILES & FOLDERS  ·  INDEXING " + Math.min(99, Math.floor(indexed * 100 / target)) + "%"
    return "FILES & FOLDERS"
  }

  Timer {
    id: debounce
    interval: 110
    repeat: false
    onTriggered: {
      searchProc.serial = root.requestSerial
      searchProc.requestQuery = root.pendingQuery
      searchProc.requestExplicit = root.pendingExplicit
      searchProc.requestContent = root.pendingContent
      searchProc.collected = ""
      var args = [
        root.binary, "launcher", "search",
        "--query", root.pendingQuery,
        "--cwd", root.cwd,
        "--limit", String((root.pendingExplicit || root.pendingContent) ? 8 : 6),
        "--compact"
      ]
      if (root.pendingContent) args.push("--content")
      searchProc.command = args
      searchProc.running = true
    }
  }

  Process {
    id: searchProc
    property int serial: 0
    property string requestQuery: ""
    property bool requestExplicit: false
    property bool requestContent: false
    property string collected: ""

    stdout: SplitParser {
      onRead: function(data) { searchProc.collected += data + "\n" }
    }

    onExited: function(exitCode, exitStatus) {
      if (searchProc.serial !== root.requestSerial) return

      root.query = searchProc.requestQuery
      root.explicitMode = searchProc.requestExplicit
      root.contentMode = searchProc.requestContent
      root.busy = false
      if (exitCode !== 0 || exitStatus !== 0) {
        root.rows = []
        root.error = "Synchro search unavailable"
        root.changed()
        return
      }

      try {
        var result = JSON.parse(searchProc.collected.trim())
        root.rows = Array.isArray(result.rows) ? result.rows : []
        root.indexStatus = result.index || ({})
        root.error = result.ok === false ? String(result.error || "Search failed") : ""
      } catch (e) {
        root.rows = []
        root.error = "Invalid Synchro search response"
      }
      root.changed()
    }
  }
}
