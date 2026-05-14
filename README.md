# NSBEdit

A lightweight, standalone RTF notepad for Windows. **v2026.05.14.10**

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


## Changelog

See [Changelog.html](Changelog.html) for full version history.

## License

GNU General Public License v2 — see [GPLv2.md](GPLv2.md).
