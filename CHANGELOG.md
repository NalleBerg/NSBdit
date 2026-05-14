# Changelog

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
