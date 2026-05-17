# NSBEdit

A lightweight, standalone RTF notepad for Windows. **v2026.05.17.15**

## Download

Just grab **[NSBEdit.exe](NSBEdit.exe)** — no installer, no extra files, no dependencies. Drop it anywhere and run it.

## Features

- Full RTF formatting toolbar: Bold, Italic, Underline, Strikethrough, Subscript, Superscript
- Font face, size, text colour, highlight colour
- Paragraph alignment (left / centre / right / justify)
- Bullet and numbered lists
- Image insertion (PNG / JPEG embedded in RTF)
- **Table insertion and editing** — Table Properties dialog (rows, columns, width, borders, padding, row height, alignment); insert from toolbar drop-down or dialog; right-click in table for Table Properties
- Alter current table or insert nested table in cell — mode radio in the dialog when caret is inside a table
- **Hyperlink insertion** — Insert Hyperlink dialog (URL + display text); inserts a proper RTF `\field` hyperlink at the caret
- **Ctrl+Click to follow hyperlinks** — extracts URL from the RTF field instruction and opens in the default browser
- **Hover tooltip on hyperlinks** — shows the URL and a Ctrl+Click hint as a two-line tooltip when hovering over a link
- **URL validation** — regex check on save: requires a recognised scheme, valid host, and 2–4 character TLD
- File menu: New, Open, Save, Save As, Print, Export as PDF (`Ctrl+Shift+P`)
- Edit menu: Undo, Redo, Cut, Copy, Paste, Select All — greyed dynamically
- Right-click context menu on the editor with the same Edit operations
- Export as PDF via *Microsoft Print to PDF* — no third-party libraries
- Keyboard Shortcuts dialog (`F1`) — 40 shortcuts, bold/colour-coded
- `Ctrl+W` to close tab / exit (with unsaved-change prompt)
- Tabbed editor — multiple documents open simultaneously, owner-drawn × close glyphs, [+] new-tab button, right-click tab context menu
- Custom status bar with real-time word/char count and Saved/Unsaved indicator (shell32 icons)
- Custom save-changes dialog with icon buttons (Save / Don't Save / Cancel)
- On-focus external file change detection with Reload / Keep Current dialog
- Owner-draw menus at 12pt Segoe UI — white background, correct highlight/grayed states
- All UI fonts at 12pt Segoe UI, DPI-aware — readable at any screen resolution or scale
- Smart toolbar reflow — controls drop one at a time as the window narrows
- Minimum window width enforced (never narrower than 3 controls per toolbar row)
- Full i18n — all UI strings through embedded locale (en_GB)
- DPI-aware (PerMonitorV2), statically linked — no external DLLs beyond Windows system ones
- Hover tooltips on all toolbar controls
- Credits dialog (About → Credits): Scintilla, Lexilla, GDI+, MinGW-W64, SQLite3, libcurl/libssh2, rtf2html sections with links
- **Syntax highlighting** — 25 languages (PHP, Python, C/C++, JavaScript, HTML, CSS, SQL, and more); choose via Language menu. Selecting a language on a plain-text tab instantly converts it to the Scintilla code editor with full colour coding
- **Typeahead autocomplete** — custom popup (yellow/green, matching tooltip style) for both keyword and phrase completion. Type part of a keyword or a phrase already in the document and pick from the list with ↑/↓/Tab/Enter or mouse click
- **Auto-close bracket and quote pairs** — typing `{`, `[`, `(`, `"`, `'`, or `«` inserts the matching closer and places the caret between them; typing a closing character when the same closer already follows the caret jumps over it. Works in both RichEdit and Scintilla editors
- **Save to FTP** (File → Save to FTP…) — upload the active document to any connected FTP server; a profile-picker list lets you choose any connected server explicitly (useful to deploy a file to a different server). Browse the remote tree, enter a filename, click Save here. Connection stays open after upload
- **Export as HTML 5** (Convert → Export as HTML 5…, RTF documents only) — converts the active RTF document to a self-contained HTML5 file with all images embedded as base64 data URIs. Norwegian and other non-ASCII characters are encoded correctly as UTF-8
- **Horizontal Rule** — insert a styled divider line (single, thick, double, dotted, dashed, or hairline) via the toolbar. Colour, width %, and indent are configurable. Behaves like a character: Delete or Backspace from the adjacent line removes it; Enter above moves it down; Ctrl+Z restores it
- **RTF formatting toolbar shown on startup** — the app opens with the Rich Text toolbar active without needing to open a file first
- **File → Open reuses blank tab** — opening a file when the only open tab is a fresh untitled one loads into that tab rather than creating an extra blank alongside it

## Building from source

Requirements: MinGW-w64 (GCC 13+) with `g++` and `windres` on PATH.

```
.\makeit.bat
```

Optionally run `NewVersion.ps1` first to stamp the build date into the About dialog.

## Changelog

See [Changelog.html](Changelog.html) for full version history.

## License

GNU General Public License v2 — see [GPLv2.md](GPLv2.md).
