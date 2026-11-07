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
- `Highlight at caret` in the same menu forces a highlight at the current caret
  and reports whether a column/value was found (v1.0.1 diagnostic).

## Changelog

### v1.0.4 (2026-11-08)
Version bump only — identical code to v1.0.3. v1.0.3 was never installed
on the real machine, so the version was bumped to 1.0.4 for a clean
first test of the fixed build. (Correction to the v1.0.3 note below:
`SCI_GETTEXTRANGE` (2162) does **not** return the SQL in the SciTE DLL —
it crashes inside Scintilla.dll at the same offset for every range, so
the plugin does not use it at all; only `SCI_GETTEXT` was verified to
return the exact SQL on real Windows.)

### v1.0.3 (2026-11-08)
True root cause found, by checking our hand-written `src/Scintilla.h`
against the official Scintilla 5.6.9 header: the v1.0.2 diagnose log
(`docLen=83` correct, 83 bytes read back all `\x00`, `no-insert`) was not
a Scintilla quirk — our `SCI_GETTEXTRANGE` constant was **2166**, but
2166 is actually `SCI_LINEFROMPOSITION` (the real number is **2162**).
The plugin was sending "give me the line number" instead of "give me the
text", so Scintilla wrote nothing and the plugin never saw the SQL. The
struct was fine; the message number was wrong. The Linux fake tests used
the same header, so wrong number met wrong number and all 28 tests passed
while the real thing did nothing. Three more copy errors in the same
header are fixed too: `SCI_INDICSETALPHA` 2083 → 2523 (2083 is
`INDICGETFORE`, so transparency was never set), `SCI_INDICSETOUTLINEALPHA`
2208 → 2558, `SCN_DOUBLECLICK` 2103 → 2006 (double-click notifications
never matched, so double-click-to-highlight was dead as well). Fixes:
- All message numbers audited against the official header; a regression
  test pins every constant to its official value so this can never recur.
- Document text is now read with `SCI_GETTEXT` (2182, verified correct —
  one plain `char*` argument, no struct); `SCI_GETTEXTRANGE` is no longer
  used anywhere. `diagnose()` now reports `gettext-empty` if `SCI_GETTEXT`
  also comes back empty while the document is non-empty.
- Verified on real Windows (GitHub Actions, real Scintilla 5.6.9 from
  SciTE): the old number 2166 reproduces the all-`\x00` failure, and
  `SCI_GETTEXT` returns the exact SQL. `SCI_GETTEXTRANGE` (2162) was
  not verified — it crashes inside Scintilla.dll at the same offset
  for every range (see v1.0.4 note).

### v1.0.2 (2026-11-08)
Real-device bug, continued: v1.0.1's log proved the notifications arrive and
the indicators allocate (base 9: value=9, column=10), yet `analyse()` returned
false for every caret position — including a manual `Highlight at caret` at
pos 19, i.e. inside `id` — so the failure is inside parsing itself, not in
notification timing or indicator allocation. Changes:
- `std::regex` is gone from the parser: `INSERT INTO` / `VALUES` are now
  matched by a hand-written ASCII case-insensitive keyword search with word
  boundaries (MSVC's `std::regex` on Chinese Windows is the prime suspect,
  since the same SQL passed the parser in tests on Linux).
- New `diagnose()` writes one line to `%TEMP%\SqlColumnHighlighter.log` on
  every failed probe: document length, statement length, the parse stage
  that failed (`no-insert` / `no-values` / `no-rows` / `parsed-no-hit` …),
  and an escaped preview of the first 120 bytes of the statement, so the
  log now shows exactly what text the plugin read and where parsing stopped.

### v1.0.1 (2026-11-07)
Real-device bug: v1.0.0 showed no highlight at all on Windows (Notepad++ 8.9.9,
Plugins Admin install) for `INSERT INTO user (id, name, age) VALUES (1, '张三', 25), (2, '李四', 30);`,
while the parser passed the same SQL in tests. Fixes:
- `SCN_UPDATEUI` no longer returns early when there is a selection (a double-click
  creates one); the selection start + 1 is now probed, so double-clicked columns
  and values both highlight even if `SCN_DOUBLECLICK` arrives late or not at all.
- Indicators are now allocated with `NPPM_ALLOCATEINDICATOR` (v8.5.6+) instead of
  hard-coded 8/9, falling back to 8/9 if allocation fails.
- New `Highlight at caret` menu command bypasses notifications entirely, so a
  manual trigger distinguishes "notification never arrived" from "drawing failed".
- Debug log at `%TEMP%\SqlColumnHighlighter.log` records every caret/double-click
  probe and `analyse()` result.

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
build.ps1          local build script (also compiles & runs the tests below)
tests/
  test_highlighter.cpp  end-to-end tests: fake Scintilla drives analyse/highlight
  test_load.cpp         DLL load smoke test (exports, getName, getFuncsArray)
  fake_sci.h            redirects ::SendMessage to the fake (via cl /FI)
.github/workflows/build-and-release.yml   CI: build x86/x64, test, zip, release
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
