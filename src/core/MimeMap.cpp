#include "MimeMap.h"

#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMimeType>

QString MimeMap::mimeForFile(const QString &path) const {
  const QFileInfo fi(path);
  if (fi.isDir())
    return QStringLiteral("inode/directory");
  const QMimeDatabase::MatchMode mode =
      fi.suffix().isEmpty() ? QMimeDatabase::MatchContent
                            : QMimeDatabase::MatchExtension;
  return m_db.mimeTypeForFile(path, mode).name();
}

QString MimeMap::iconNameForMime(const QString &mimeName) const {
  if (mimeName == QLatin1String("inode/directory"))
    return QStringLiteral("folder");
  const QMimeType mime = m_db.mimeTypeForName(mimeName);
  QString icon = mime.genericIconName();
  if (icon.isEmpty())
    icon = mime.iconName();
  if (icon.isEmpty())
    icon = QStringLiteral("application-x-executable");
  return icon;
}

QString MimeMap::iconNameForFile(const QString &path) const {
  return iconNameForMime(mimeForFile(path));
}

namespace {

bool hasTextSuffix(const QString &path) {
  static const char *const kSuf[] = {
      ".txt",  ".log",   ".csv",   ".tsv",     ".json",  ".jsonc", ".json5",
      ".xml",  ".yml",   ".yaml",  ".toml",    ".ini",   ".cfg",   ".conf",
      ".config", ".cnf", ".env",   ".envrc",   ".properties",
      ".md",   ".markdown", ".rst", ".adoc",   ".tex",
      ".c",    ".h",     ".cc",    ".hh",      ".cpp",   ".hpp",   ".cxx",
      ".rs",   ".go",    ".py",    ".pyi",     ".js",    ".ts",    ".jsx",
      ".tsx",  ".mjs",   ".cjs",   ".qml",     ".lua",   ".rb",    ".php",
      ".pl",   ".java",  ".kt",    ".kts",     ".swift", ".cs",
      ".sh",   ".bash",  ".zsh",   ".fish",    ".ps1",
      ".cmake", ".mk",   ".diff",  ".patch",
      ".html", ".htm",   ".css",   ".scss",    ".vue",
      ".sql",  ".graphql", ".gql",  ".proto",
      ".tf",   ".hcl",   ".nix",   ".vim"};
  for (const char *s : kSuf) {
    if (path.endsWith(QLatin1String(s), Qt::CaseInsensitive))
      return true;
  }
  return false;
}

bool isWellKnownTextName(const QString &path) {
  const QString name = QFileInfo(path).fileName();
  static const char *const kExact[] = {
      "dockerfile",   "containerfile", "makefile",     "gnumakefile",
      "bsdmakefile",  "gemfile",       "rakefile",     "procfile",
      "vagrantfile",  "brewfile",      "justfile",     "jenkinsfile",
      "podfile",      "fastfile",      "guardfile",    "capfile",
      "dangerfile",   "taskfile",      "steepfile",    "berksfile",
      "license",      "licence",       "copying",      "authors",
      "contributors", "changelog",     "changes",      "news",
      "todo",         "readme",        "notice",       "codeowners",
      "security",     ".gitignore",    ".gitattributes", ".gitmodules",
      ".gitkeep",     ".dockerignore", ".editorconfig", ".clang-format",
      ".clang-tidy",  ".npmrc",        ".nvmrc",       ".bashrc",
      ".zshrc",       ".profile",      ".zprofile",    ".bash_profile",
      ".gitconfig",   ".mailmap"};
  const QString lower = name.toLower();
  for (const char *n : kExact) {
    if (lower == QLatin1String(n))
      return true;
  }
  return lower.startsWith(QLatin1String("dockerfile")) ||
         lower.startsWith(QLatin1String("makefile"));
}

bool isTextApplicationMime(const QString &mime) {
  static const char *const kMime[] = {
      "application/json",     "application/xml",
      "application/javascript", "application/sql",
      "application/yaml",     "application/x-yaml",
      "application/toml",     "application/x-desktop",
      "application/x-shellscript", "application/x-sh",
      "application/x-ruby",   "application/x-python",
      "application/x-perl",   "application/x-php",
      "application/xhtml+xml", "application/graphql",
      "application/x-wine-extension-ini", "inode/x-empty"};
  for (const char *m : kMime) {
    if (mime == QLatin1String(m))
      return true;
  }
  return false;
}

bool sniffUtf8Text(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  const QByteArray raw = f.read(4096);
  if (raw.isEmpty())
    return true;
  if (raw.contains('\0'))
    return false;
  int bad = 0;
  for (unsigned char c : raw) {
    if (c == '\t' || c == '\n' || c == '\r')
      continue;
    if (c < 0x20)
      ++bad;
  }
  return bad * 10 < raw.size();
}

} // namespace

bool MimeMap::isProbablyText(const QString &path, const QString &mimeHint) {
  const QFileInfo fi(path);
  if (fi.isDir())
    return false;
  QString mime = mimeHint.toLower();
  if (mime.isEmpty()) {
    MimeMap map;
    mime = map.mimeForFile(path).toLower();
  }
  if (mime.startsWith(QLatin1String("text/")))
    return true;
  if (isTextApplicationMime(mime))
    return true;
  const bool named = hasTextSuffix(path) || isWellKnownTextName(path);
  if (mime.startsWith(QLatin1String("image/")) ||
      mime.startsWith(QLatin1String("audio/")) ||
      mime == QLatin1String("application/pdf") ||
      mime == QLatin1String("application/zip") ||
      mime.startsWith(QLatin1String("application/x-tar")) ||
      mime.startsWith(QLatin1String("application/vnd."))) {
    if (!named)
      return false;
  }
  if (named)
    return sniffUtf8Text(path);
  if (mime.startsWith(QLatin1String("video/")) && !named)
    return false;
  if (mime == QLatin1String("application/octet-stream") || mime.isEmpty())
    return sniffUtf8Text(path);
  return false;
}

QString MimeMap::resolveIcon(const QString &iconName) const {
  if (!iconName.isEmpty() && !QIcon::fromTheme(iconName).isNull())
    return iconName;
  const QString fallback = QStringLiteral("application-x-executable");
  if (!QIcon::fromTheme(fallback).isNull())
    return fallback;
  return iconName.isEmpty() ? fallback : iconName;
}
