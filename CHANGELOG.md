# Changelog

## v2026.05.18.12 - 18.05.2026 12:19

- **Scintilla word wrap ↵ indicator**: A teal-green ↵ glyph now appears at the right edge of every wrapped visual sub-line in Scintilla (code) tabs when word wrap is on. Implemented via `SCN_PAINTED` — Scintilla's documented post-paint notification — rather than a `WM_PAINT` subclass (Scintilla's own caret/selection repaints were overwriting the subclass overlay). The glyph is drawn to the left of the custom MSB vertical scrollbar; `WS_CLIPSIBLINGS` on the Scintilla window was silently clipping the previous attempt into invisibility. New helper: `Ne_DrawSciWrapIndicators(hSci)` called from `WM_NOTIFY → SCN_PAINTED`.
- **[+] new-tab button always visible**: `NeTabs_TabProc` `WM_PAINT` now calls `RedrawWindow(hBtnNew, RDW_INVALIDATE | RDW_UPDATENOW)` after painting the tab strip, so the [+] button is never left erased when Windows theming overdraw covers the sibling button area.
- **Edition 1 in About dialog**: The About dialog now shows `Edition: 1` below the version line. Locale key `ABOUT_EDITION` added to `locale/en_GB.txt`.

## v2026.05.18.09 - 18.05.2026 09:08

- **Paragraph Spacing dialog restyled and fixed**: The Paragraph Spacing dialog (Format → Paragraph Spacing) now uses the app-standard owner-draw button system (`NeBtnTone` / `NeDialogButtonSpec` / `Ne_DrawDialogButton` / `Ne_BtnHoverProc`) with a white background, blue Save and red Cancel buttons, and hover-highlight. Root cause of Save/Cancel doing nothing fixed: the dialog class previously used `DefWindowProcW` as its `WndProc`, which silently discarded the `WM_COMMAND` sent synchronously by button clicks — the `GetMessageW` loop never saw it. The dialog now has a proper `Ne_ParSpaceDlgProc` that reads the spin-box values on IDOK, stores them in module-level statics, and calls `DestroyWindow`; the message loop exits when `IsWindow(dlg)` returns false, and the values are applied afterwards. All strings go through `Ls()` (i18n-correct: `DLG_PARSPACE`, `DLG_PARSPACE_BEF`, `DLG_PARSPACE_AFT`, `BTN_SAVE`, `BTN_CANCEL`).
- **Line Spacing dialog restyled and fixed**: Same root-cause fix as Paragraph Spacing. The Line Spacing dialog (Format → Line Spacing) now has `Ne_LineSpaceDlgProc` as its WndProc; it reads the selected radio button on IDOK, stores the rule in `s_lineSpRule`, and calls `DestroyWindow`. Owner-draw blue Save / red Cancel buttons with `Ne_BtnHoverProc` hover tracking; white background with `WM_CTLCOLORBTN` so radio-button backgrounds match. All strings i18n via `Ls()` (`DLG_LINESPACE`, `RDO_LINESPACE_S`, `RDO_LINESPACE_15`, `RDO_LINESPACE_D`, `BTN_SAVE`, `BTN_CANCEL`).

## v2026.05.17.15 - 17.05.2026 15:07

- **Horizontal Rule (HR) in RTF documents**: HR paragraphs render as a custom-drawn line across the editor in one of six styles — single, thick, double, dotted, dashed, or hairline. Colour (solid or gradient), width %, and left/right indent are configurable via a properties dialog. Core functions: `Ne_InsertHRule`, `Ne_PaintHRules`, `Ne_RebuildHRList` (`g_hrMap` HWND→entry cache), `Ne_DeleteHRule`, `Ne_ShowHRulePropsDialog`. Drawing is overlaid via `GetDC` after `CallWindowProc` in `WM_PAINT` (avoids nested-`BeginPaint` clip conflicts). HR behaves like a character that occupies its whole line: Delete at the end of the line above or Backspace at the start of the line below deletes it; pressing Enter on any line above an HR moves it one line down; Ctrl+Z correctly restores a deleted HR. Three bugs fixed during development: (a) typing on a line above the HR used to draw the HR through the text — `EN_CHANGE → Ne_RebuildHRList` now keeps `charIdx` current; (b) pressing Enter above the HR used to erase it — `NE_WM_HR_CLEANUP` (posted via `PostMessageW`) now receives `enterPos` in `wParam` and strips only the paragraph at that position if it inherited HR format, keeping the undo chain intact and avoiding re-entrant `EM_EXSETSEL` calls during mid-split RichEdit processing; (c) `Ctrl+Z` after deleting an HR now correctly redraws it — the `EN_CHANGE` guard that skipped `Ne_RebuildHRList` when `g_hrMap` was empty has been removed.
- **Status bar shows "Rich text"** on RTF/RichEdit tabs alongside the word and character count.

## v2026.05.16.14 - 16.05.2026 14:32

- **Export as HTML 5…** (Convert menu, RTF only): converts active RTF to self-contained HTML5 with base64-embedded images. Uses `rtf2html/ne_rtf2html_lib.cpp` wrapper. Fixes: `\*\picprop` groups inside `\pict` are now skipped (their property names contain hex-like letters that were corrupting image data); `char_by_code()` in `rtf_tools.h` now emits proper UTF-8 (CP1252 → Unicode → UTF-8) instead of raw bytes — Norwegian/non-ASCII characters now render correctly. Menu item greyed for non-RTF tabs.
- **RTF toolbar on startup**: `Ne_DocIsRtf()` returns `true` for untitled RichEdit tabs; `Ne_UpdateToolbarMode` uses `Ne_DocIsRtf` — Rich Text toolbar shows immediately on launch without opening a file first.
- **File → Open reuses untitled tab**: if the active tab is an untouched untitled RichEdit tab, the opened file loads into it directly instead of creating a new blank tab.

## v2026.05.16.11 - 16.05.2026 11:41

- MSB custom scrollbars now on Scintilla code tabs in addition to RichEdit: `s_sciSbV`/`s_sciSbH` maps, `Ne_AttachSciScrollbars`, `Ne_DetachSciScrollbars`. `SCN_UPDATEUI` → `msb_sync` keeps the thumb in sync during keyboard scrolling. Bug fix: `Ne_AttachScrollbars` was missing from the RTF branch of `Ne_LoadPathIntoEditor` — added.
- Auto-close bracket/quote pairs (both RichEdit and Scintilla): typing `{`, `[`, `(`, `"`, `'`, `«` inserts the matching closer and leaves the caret between them. Typing a closing char when the same char already follows the caret jumps over it instead of inserting a duplicate. `Ne_SciAutoPair` (called from `SCN_CHARADDED`) handles Scintilla; a `WM_CHAR` handler in `Ne_EditCaretProc` handles RichEdit. The RichEdit handler also wraps selected text when an opener is typed with a non-empty selection.
- Save to FTP — profile list picker (`Ne_ShowFtpSelectDialog` / `Ne_FtpSelectDlgProc`): connected profiles shown as a vertically-stacked list of full-width blue owner-draw buttons, no server cap (old code was limited to 3). Even a single connected profile requires explicit selection. Not-connected dialog message corrected: was showing `FTP_STATUS` = "Connected:", now shows `FTP_NOT_CONNECTED`.
- Locale: `FTP_NOT_CONNECTED`, `FTP_PICK_PROFILE` added to `locale/en_GB.txt`.

## v2026.05.16.10 - 16.05.2026 10:47

- Custom autocomplete popup component (`ne_autocomplete/`): `NsbAutoComplete` window class, `CS_DROPSHADOW`, `WS_EX_NOACTIVATE | WS_EX_TOPMOST`. Appearance matches the tooltip style — system tooltip yellow background, dark amber border `RGB(120,100,20)`, muted sage green selection `RGB(80,160,110)` with white text. DPI-aware 12pt Segoe UI font via `GetDpiForWindow` + `MulDiv`.
- Replaces `SCI_AUTOCSHOW` for both keyword and phrase-completion modes. Scintilla's built-in popup cancelled itself when the entered text contained non-word characters (spaces, `=`, `$`); the custom popup has no such restriction.
- Phrase completion: when the line-prefix (trimmed) contains a space, all matching whole lines from the document are collected as candidates (case-insensitive prefix match, deduplicated with `unordered_set`, up to 30 results, sorted).
- Keyword completion: same custom popup as phrase mode — consistent yellow/green look, same keyboard and mouse behaviour.
- Popup shows up to 9 items; scrollable with ▲/▼ arrows and mouse wheel when list is longer.
- Keyboard: ↑/↓ navigate; Tab/Enter accept; Escape dismiss; Backspace/Delete dismiss and pass through to Scintilla. Tab/Enter acceptance: `WM_KEYDOWN` consumed via `pendingAccept` flag; next `WM_CHAR` swallowed so no stray character is inserted into the document.
- Mouse click acceptance: `WM_MOUSEACTIVATE → MA_NOACTIVATE` + `WM_KILLFOCUS` guard (skip dismiss if focus went to popup window) ensures item clicks always work.
- Scintilla HWND subclassed while popup is visible; restored on every dismiss path. `g_acInserting` flag prevents autocomplete re-triggering during `SCI_REPLACESEL` insertion.
- Popup positioned below caret line; flips above if it would extend past the monitor bottom edge.
- `makeit.bat`: taskkill output now shown (was suppressed); 1-second `timeout` added after kill so the linker never fails with "Permission denied" on `NSBEdit.exe` when the app was still running.
- New files: `ne_autocomplete/ne_autocomplete.h`, `ne_autocomplete/ne_autocomplete.cpp`, `API_INTERNALS/API/ne_autocomplete_API.txt`.

## v2026.05.16.09 - 16.05.2026 09:58

- RichEdit line-number gutter (`NsbLineGutter`): custom child window class that renders line numbers alongside RichEdit tabs. Always present as a thin strip (S(20)) even when numbers are off; expands to full width (S(44)) when on.
- `Ne_EnsureLineGutter()`: creates the `NsbLineGutter` window for a RichEdit doc and attaches a tooltip ("Show / Hide line numbers").
- `Ne_SyncRichGutters()`: repositions all gutters and trims the editor rect after `NeTabs_SetRects`. Scintilla tabs get the thin strip only (Scintilla draws its own margin); RichEdit tabs get full or thin width depending on `s_lineNumsOn`.
- `Ne_SyncLineNumBtn()` rewritten: iterates all tabs, sets `SCI_SETMARGINWIDTHN` on Scintilla windows and invalidates / updates tooltip on RichEdit gutters. Calls `Ne_SyncRichGutters` at the end.
- `s_lineNumsOn` global persists across tab switches; `Ne_SetupScintillaStyle` reads it to set the initial margin width.
- `NeTabDoc::hLineGutter` field added to store the companion gutter HWND.
- Autocomplete: `SCI_AUTOCSETIGNORECASE TRUE` set in `Ne_SetupScintillaStyle`.

## v2026.05.15.16 - 15.05.2026 16:56

- Ne_ApplyLang(hSci, langIdx) added: sends keyword list via SCI_SETKEYWORDS and applies per-lexer style overrides. Previously no keywords reached the lexer so all text stayed black.
- PHP switched from hypertext to phpscript lexer; SCE_HPHP_* style IDs (118-127) mapped: strings red, keywords blue/bold, $variables purple, comments green italic, numbers green.
- Language menu on RichEdit tab now converts to Scintilla on demand: extracts plain text, creates Scintilla at same position, loads text, hides RichEdit, applies chosen lexer.
- AltGr fix: Ctrl-shortcut intercept in message loop now skips when VK_RMENU (Right Alt / AltGr) is held — AltGr+0 (}), AltGr+7 ({) etc. now reach the editor correctly.
- File > Save to FTP... (IDM_SAVE_TO_FTP): uploads active document to any connected FTP server. FTP browser in save mode — filename edit + Save here (green) / Cancel (red) buttons. Keeps connection open. Marks tab as FTP-linked on success.
- Ne_ShowFtpBrowserSave: save-mode FTP browser dialog — filename label + edit above tree, Save here / Cancel at bottom, Refresh top-right.
- Locale: MENU_SAVE_TO_FTP, FTP_SAVE_BROWSER, FTP_FILENAME_PROMPT, FTP_SAVE_HERE, FTP_SAVE_TO, FTP_SAVED_OK added to locale/en_GB.txt.

## v2026.05.15.12 - 15.05.2026 12:15

- Credits dialog added: accessible via About → Credits. Sections for Scintilla, Lexilla, GDI+, and MinGW-W64, each with description and link. Rendered in a RichEdit pane with colour-coded headers.
- About, License, and Credits dialogs converted to owner-draw button system (NeBtnTone / NeDialogButtonSpec / Ne_DrawDialogButton / Ne_BtnHoverProc) — DPI-aware measured widths, hover highlight, icon + text layout.
- Locale additions: ABOUT_BTN_CREDITS, ABOUT_BTN_CLOSE added to locale/en_GB.txt.

## v2026.05.14.10 - 14.05.2026 10:51

- Added Insert Hyperlink dialog (URL + display text, Tab navigation, Save/Cancel owner-draw buttons).
- URL field validated with std::wregex: requires scheme (https/http/ftp(s)/mailto/file/www.), host with dot-separated labels, and 2–4 alpha TLD.
- Invalid URL shows custom NSBEdit warning dialog (IDI_WARNING icon + OK button) instead of MessageBoxW.
- Ctrl+Click follows hyperlinks: Ne_EditCaretProc intercepts WM_LBUTTONDOWN while cursor is IDC_HAND, extracts URL from RTF field instruction via Ne_ExtractLinkUrlAt, opens with ShellExecuteW.
- Hover tooltip on hyperlinks: two-line ShowMultilingualTooltip (URL on line 1, Ctrl+Click hint on line 2) triggered by IDC_HAND cursor detection in WM_MOUSEMOVE subclass.
- Ne_ShowChoiceDialog extended with optional hMsgIcon parameter; dialog font upgraded from DEFAULT_GUI_FONT to 12pt Segoe UI (Ne_CreateDialogFont), stored in NeDialogData and freed on WM_NCDESTROY.
- ENM_LINK added to all EM_SETEVENTMASK calls; EN_LINK WM_LBUTTONDOWN fallback handler in WM_NOTIFY.
- Link dialog: static bool registered guard, COLOR_WINDOW+1 background, WM_CTLCOLORSTATIC, AdjustWindowRectEx sizing, button height S(34).
- API/INTERNALS rebrand: all SetupCraft references in API_INTERNALS/**/*.txt replaced with NSBEdit.
- Locale: LINK_TIP_CTRL, MSG_LINK_BAD_URL, BTN_OK added to locale/en_GB.txt.

## v2026.05.11.13 - 11.05.2026 13:11

- Table properties dialog: Apply and Cancel owner-draw buttons now work correctly.
- Table values (rows, cols, column width, borders, padding, row height, alignment) are read in the window procedure before DestroyWindow, stored in a module-level NeTableProps struct, so Apply always inserts/alters the table with the user's chosen values.
- Apply button inserts a new table when caret is outside a table.
- When caret is inside a table, a mode radio group appears at the top of the dialog: "Alter current table" (pre-selected) or "Table in current cell". Alter mode replaces the table by scanning \intbl paragraphs around the caret; nested mode inserts a fresh table at the caret position.
- Buttons centred horizontally in the dialog.
- All new strings fully localised: TBLP_MODE_ALTER, TBLP_MODE_NESTED added to locale/en_GB.txt.

## v2026.05.11.09 - 11.05.2026 09:59

- All UI fonts unified to 12pt Segoe UI (DPI-aware via GetDpiForWindow/GetDpiForSystem) — toolbar, dialogs, status bar, tooltips.
- Owner-draw menus with 12pt Segoe UI: white background, correct highlight/grayed colours, right-aligned accelerator text.
- Menu bar items (File/Edit/Help) also owner-drawn at 12pt via Ne_AppendMenuOD(isBar=true).
- Status bar: correct shell32.dll icons — index 294 (green checkmark = Saved), index 131 (red X = Unsaved).
- Added ne_statusbar.h/cpp: custom owner-drawn status bar with word/char count and Saved/Unsaved status with shell32 icons.
- Added ne_tabs.h/cpp: tabbed editor with owner-drawn × close glyphs, hover highlight, [+] new-tab button, right-click context menu.
- Tab context menu: New Tab / Close Tab (localized).
- Ctrl+W with one tab closes the app; with multiple tabs closes the active tab.
- Softer owner-draw dialog button colours with hover state tracking.

## v2026.05.11.08 - 11.05.2026 08:38

- Replaced the system save-changes MessageBox with a custom NSBEdit modal dialog.
- Added owner-draw icon buttons for Save, Don't Save, and Cancel.
- Added focus-based external file-change detection using file stamp comparison.
- Added a custom Reload/Keep Current dialog when a file changed on disk while unfocused.
- Added reusable Ne_ShowChoiceDialog and unified Ne_LoadPathIntoEditor load path.
- Added localized keys for new dialog titles, prompts, and button labels.

## v2026.05.10.16 - 10.05.2026 16:25

- Added Edit menu: Undo, Redo, Cut, Copy, Paste, Select All.
- Added right-click context menu on RichEdit with dynamic enabled/disabled states.
- Added Export as PDF via Microsoft Print to PDF (Ctrl+Shift+P).
- Added Keyboard Shortcuts dialog (F1) with bold shortcut column and royal-blue descriptions.
- Added Ctrl+W shortcut for Exit and updated menu accelerator hints.
