# SQL Column Highlighter for Notepad++

A Notepad++ plugin: in a SQL `INSERT` statement, **click a column name to highlight its value(s)**, and **double-click a value to highlight its column**.

```sql
INSERT INTO users (id, name, email) VALUES (1, 'Alice', 'a@example.com');
--               ^^^^  click "name"  ->  'Alice' is highlighted amber
--  double-click 'a@example.com'  ->  "email" is highlighted blue
```

- Click a column name → the corresponding value is highlighted amber (for multi-row
  `VALUES`, the same column in every row is highlighted); the column itself is blue.
- Double-click a value → the corresponding column is highlighted blue.
- Double-click a column name → same as clicking: its value(s) are highlighted.
- Highlights are cleared automatically when the caret moves away or you switch files.
- Can be toggled from the plugin menu (`Plugins → SQL Column Highlighter`).

The parser is quote/comment aware: commas inside strings (`'x, y'`), escaped quotes
(`'it''s'`), nested function calls (`UPPER('a')`), multi-line statements, multi-row
`VALUES`, `` ` `` / `" "` / `[ ]` quoted identifiers, and `--` / `#` / `/* */` comments
are all handled.

## Install

**Via Plugins Admin** (once listed): `Plugins → Plugins Admin…`, search
`SQL Column Highlighter`, install.

**Manual**: download `SqlColumnHighlighter-<version>-x64.zip` (or `-x86.zip`) from
[Releases](../../releases), extract `SqlColumnHighlighter.dll` into
`%PROGRAMFILES%\Notepad++\plugins\SqlColumnHighlighter\` (create the folder),
restart Notepad++.

## Build

No `.vcxproj` needed — `build.ps1` invokes `cl.exe`/`link.exe` directly.

Locally (in an "x64 Native Tools Command Prompt for VS"):

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Arch x64
```

CI: pushing a tag like `v1.0.0` triggers `.github/workflows/build-and-release.yml`,
which builds x86 + x64 on `windows-latest`, zips each DLL (DLL at zip root, as
Plugins Admin requires), and creates a GitHub Release with SHA-256 checksums.

## Project layout

```
src/
  PluginMain.cpp   plugin entry: menus, notification dispatch
  Highlighter.cpp  INSERT parsing + indicator highlighting
  Highlighter.h
  NppApi.h         minimal Notepad++ plugin interface
                   (values verified against the official Notepad_plus_msgs.h)
  Scintilla.h      minimal Scintilla constants used
  version.rc       DLL version resource (1.0.0.0)
build.ps1          local build script
.github/workflows/build-and-release.yml   CI: build x86/x64, zip, release
```

## Submitting to Plugins Admin (nppPluginList)

1. Tag a release (`git tag v1.0.0 && git push origin v1.0.0`); CI publishes the zips.
2. Take the SHA-256 of each zip (printed in the workflow log and `SHA256SUMS.txt`).
3. Fork [notepad-plus-plus/nppPluginList](https://github.com/notepad-plus-plus/nppPluginList)
   and add an entry to `src/pl.x64.json` / `src/pl.x86.json`:

```json
{
    "folder-name": "SqlColumnHighlighter",
    "display-name": "SQL Column Highlighter",
    "version": "1.0.0.0",
    "id": "<sha256 of the zip>",
    "repository": "https://github.com/zhgao/npp-sql-column-highlighter/releases/download/v1.0.0/SqlColumnHighlighter-v1.0.0-x64.zip",
    "description": "Click a column name to highlight its value(s); double-click a value to highlight its column, in SQL INSERT statements.",
    "author": "zhgao",
    "homepage": "https://github.com/zhgao/npp-sql-column-highlighter"
}
```

4. Open a PR against `notepad-plus-plus/nppPluginList`. After it is merged, the plugin
   appears in Plugins Admin with the next Notepad++ release.

## License

MIT — see [LICENSE](LICENSE).
