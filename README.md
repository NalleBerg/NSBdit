# NSBEdit

A lightweight, standalone RTF notepad for Windows. **v2026.05.10.15**

## Download

Just grab **[NSBEdit.exe](NSBEdit.exe)** — no installer, no extra files, no dependencies. Drop it anywhere and run it.

## Features

- Full RTF formatting toolbar: Bold, Italic, Underline, Strikethrough, Subscript, Superscript
- Font face, size, text colour, highlight colour
- Paragraph alignment (left / centre / right / justify)
- Bullet and numbered lists
- Image insertion (PNG / JPEG embedded in RTF)
- File menu: New, Open, Save, Save As, Print
- Status bar showing line/column and file path
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
