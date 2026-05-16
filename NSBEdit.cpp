// NSBEdit — standalone RTF notepad
// Full-featured RTF editor window with File menu, toolbar, and status bar.
// Icon: shell32.dll index 70  (set on taskbar, title bar, and status bar).
// Tooltips: English-only via project tooltip system.

#include "NSBEdit.h"
#include <windows.h>
#include <windowsx.h>
#include <richedit.h>
#include <commdlg.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <map>
#include <unordered_map>
#include <regex>
#include <stdio.h>
#include "dpi.h"
#include "tooltip/tooltip.h"
#include "ne_tabs.h"
#include "ne_statusbar.h"
#include "scroll/my_scrollbar_vscroll.h"
#include "scroll/my_scrollbar_hscroll.h"
#include "highlight/highlight.h"
#include "checkbox.h"
#include "ne_crypto.h"
#include "ne_profiles.h"
#include "ne_ftp.h"
// Scintilla + Lexilla (statically linked)
#include "ILexer.h"
#include "Scintilla.h"
#include "ScintillaMessages.h"
#include "Lexilla.h"

// ── Control and menu IDs ───────────────────────────────────────────────────────
#define IDI_APPICON         1
#define IDM_NEW             101
#define IDM_OPEN            102
#define IDM_SAVE            103
#define IDM_SAVEAS          104
#define IDM_EXIT            105
#define IDM_PRINT           106
#define IDM_ABOUT           107
#define IDM_EXPORT_PDF      108
#define IDM_SHORTCUTS       109
#define IDM_UNDO            110
#define IDM_REDO            111
#define IDM_CUT             112
#define IDM_COPY            113
#define IDM_PASTE           114
#define IDM_SELECTALL       115
#define IDM_FIND            116
#define IDM_REPLACE         117
#define IDR_LOCALE_EN_GB    10

// Convert menu
#define IDM_CONV_TO_PLAIN   120
#define IDM_ENC_UTF8        121
#define IDM_ENC_UTF16LE     122
#define IDM_ENC_ANSI        123
#define IDM_ENC_WIN1252     124
#define IDM_ENC_ISO8859_1   125
#define IDM_CONV_TO_RTF     126

enum class NeEncoding {
    Unknown  = 0,
    RichText,   // .rtf — not a text encoding, shown as "Rich text"
    UTF8,
    UTF16LE,
    ANSI,       // system default codepage
    Win1252,
    ISO8859_1,
};

#define IDC_NE_EDIT         201
#define IDC_NE_BOLD         202
#define IDC_NE_ITALIC       203
#define IDC_NE_UNDERLINE    204
#define IDC_NE_STRIKE       205
#define IDC_NE_SUBSCRIPT    206
#define IDC_NE_SUPERSCRIPT  207
#define IDC_NE_FONTFACE     208
#define IDC_NE_FONTSIZE     209
#define IDC_NE_COLOR        210
#define IDC_NE_HIGHLIGHT    211
#define IDC_NE_ALIGN_L      212
#define IDC_NE_ALIGN_C      213
#define IDC_NE_ALIGN_R      214
#define IDC_NE_ALIGN_J      215
#define IDC_NE_BULLET       216
#define IDC_NE_NUMBERED     217
#define IDC_NE_IMAGE        218
#define IDC_NE_STATUSBAR    219
#define IDC_NE_INDENT_IN    220
#define IDC_NE_INDENT_OUT   221
#define IDC_NE_LINESPACE    222
#define IDC_NE_FIND         223
#define IDC_NE_LINK         224
#define IDC_NE_TABLE        225
#define IDC_NE_TABLE_DROP   235   // ▼ split-arrow beside TABLE button
#define IDC_NE_HLINE        226
#define IDM_TABLE_PROPS     127   // Table properties (menu/button)
#define IDM_CTX_TABLE_PROPS 128   // Context-menu "Table properties"
#define IDM_CTX_HRULE_PROPS 129   // Context-menu "Horizontal Rule Properties"
#define IDM_FTP_ADD_SITE    130   // FTP menu: "Add site..."
#define IDM_FTP_DISCONNECT  131   // FTP menu: "Disconnect"
#define IDM_FTP_BROWSE      132   // FTP menu: "Browse files..."
#define IDM_SAVE_TO_FTP     133   // File menu: "Save to FTP..."
#define IDM_LANG_BASE       600   // Language menu: 600..600+NE_LANG_COUNT-1
#define IDM_FTP_CONNECT_BASE 700  // FTP menu: profile entries 700..799
#define IDC_NE_CLEARFMT     227
#define IDC_NE_PRINT_BTN    228
#define IDC_NE_ZOOM         229
#define IDC_NE_DLG_TEXT     230
#define IDC_NE_TABCTRL      231
#define IDC_NE_WORDWRAP     232
#define IDC_NE_CASE         233
#define IDC_NE_PARSPACE     234
// ── Dialog-internal control IDs ───────────────────────────────────────────────
#define IDC_NE_DLG_FIND_WHAT    240
#define IDC_NE_DLG_FIND_NEXT    241
#define IDC_NE_DLG_REPL_WITH    242
#define IDC_NE_DLG_REPLACE      243
#define IDC_NE_DLG_REPLACE_ALL  244
#define IDC_NE_DLG_LINK_URL     246
#define IDC_NE_DLG_LINK_TEXT    247
#define IDC_NE_DLG_TABLE_ROWS   248
#define IDC_NE_DLG_TABLE_COLS   249
#define IDC_NE_DLG_MATCHCASE    250
#define IDC_NE_DLG_WHOLEWORD    251
#define IDC_NE_DLG_FIND_COUNT   260
#define IDC_NE_DLG_REGEX        261
#define IDC_NE_DLG_PAR_BEF      252
#define IDC_NE_DLG_PAR_AFT      253
#define IDC_BTN_LINENUM         262   // line-number toggle button (▸/◂)

// ── Internal state ─────────────────────────────────────────────────────────────
// ── Per-tab custom scrollbar handles (keyed by hEdit HWND) ──────────────────
static std::map<HWND, HMSB> s_sbV, s_sbH;

static void Ne_AttachScrollbars(HWND hEdit)
{
    if (!hEdit) return;
    if (s_sbV.count(hEdit)) return; // already attached
    s_sbV[hEdit] = msb_attach(hEdit, MSB_VERTICAL);
    s_sbH[hEdit] = msb_attach(hEdit, MSB_HORIZONTAL);
}

static void Ne_DetachScrollbars(HWND hEdit)
{
    if (!hEdit) return;
    auto iv = s_sbV.find(hEdit);
    if (iv != s_sbV.end()) { msb_detach(iv->second); s_sbV.erase(iv); }
    auto ih = s_sbH.find(hEdit);
    if (ih != s_sbH.end()) { msb_detach(ih->second); s_sbH.erase(ih); }
}

static void Ne_DetachAllScrollbars()
{
    for (auto& kv : s_sbV) msb_detach(kv.second);
    for (auto& kv : s_sbH) msb_detach(kv.second);
    s_sbV.clear();
    s_sbH.clear();
}

// Show or hide each custom scrollbar bar window to match its edit's visibility,
// and reposition bars for visible edits. Call after any tab switch or resize.
static void Ne_SyncScrollbarVisibility(HWND hwnd)
{
    int n = NeTabs_GetCount(hwnd);
    for (int i = 0; i < n; ++i) {
        NeTabDoc* doc = NeTabs_GetDocByIndex(hwnd, i);
        if (!doc || !doc->hEdit) continue;
        bool vis = IsWindowVisible(doc->hEdit) != FALSE;
        int sw = vis ? SW_SHOWNOACTIVATE : SW_HIDE;
        auto syncBar = [&](std::map<HWND,HMSB>& m) {
            auto it = m.find(doc->hEdit);
            if (it == m.end() || !it->second) return;
            HWND hBar = msb_get_bar_hwnd(it->second);
            if (hBar) ShowWindow(hBar, sw);
            if (vis) msb_reposition(it->second);
        };
        syncBar(s_sbV);
        syncBar(s_sbH);
    }
}

struct NeState {
    bool  updatingToolbar = false;
    int   tabY            = 0;
    int   tabH            = 0;
    int   editY           = 0;
    int   editX           = 0;
    int   editW           = 0;
    int   editH           = 0;
    int   statusH         = 0;
    int   pad             = 0;
    int   minClientW      = 0;  // minimum client width enforced by WM_GETMINMAXINFO
    HICON hIconLarge      = NULL;
    HICON hIconSmall      = NULL;
    HWND  hBtnLineNum     = NULL; // line-number toggle button (▸/◂)
};

// ── Language / Scintilla support ─────────────────────────────────────────────

struct NeLang {
    const wchar_t* name;   // display name
    const char*    lexer;  // Lexilla lexer name (nullptr = Plain Text, no lexer)
    const wchar_t* exts;   // pipe-separated lowercase extensions
};

static const NeLang s_langs[] = {
    { L"Plain Text",   nullptr,       L"txt|text|log"             },
    { L"Batch",        "batch",       L"bat|cmd"                  },
    { L"C / C++",      "cpp",         L"c|cpp|cxx|cc|h|hpp|hxx|inl" },
    { L"C#",           "cpp",         L"cs"                       },
    { L"CSS",          "css",         L"css|scss|less"            },
    { L"HTML",         "hypertext",   L"html|htm|xhtml|shtml"     },
    { L"INI / Config", "props",       L"ini|cfg|conf|properties|editorconfig" },
    { L"Java",         "cpp",         L"java"                     },
    { L"JavaScript",   "cpp",         L"js|mjs|cjs"               },
    { L"JSON",         "json",        L"json|jsonc|geojson"       },
    { L"Lua",          "lua",         L"lua"                      },
    { L"Makefile",     "makefile",    L"makefile|mak|mk"          },
    { L"Markdown",     "markdown",    L"md|markdown"              },
    { L"Pascal",       "pascal",      L"pas|pp|dpr|lpr"           },
    { L"Perl",         "perl",        L"pl|pm|pod"                },
    { L"PHP",          "phpscript",   L"php|php3|php4|php5|phtml" },
    { L"PowerShell",   "powershell",  L"ps1|psm1|psd1"            },
    { L"Python",       "python",      L"py|pyw|pyi"               },
    { L"Ruby",         "ruby",        L"rb|rbw|gemspec"           },
    { L"Rust",         "rust",        L"rs"                       },
    { L"SQL",          "sql",         L"sql"                      },
    { L"TypeScript",   "cpp",         L"ts|tsx"                   },
    { L"VBScript",     "vbscript",    L"vbs"                      },
    { L"XML",          "xml",         L"xml|xsl|xslt|xsd|svg|plist|resx" },
    { L"YAML",         "yaml",        L"yaml|yml"                 },
};
static const int NE_LANG_COUNT = (int)(sizeof(s_langs) / sizeof(s_langs[0]));

// Menu handle for the Language popup (so we can update checkmarks).
static HMENU s_hLangMenu = NULL;
static HMENU s_hFtpMenu  = NULL;
// Forward declaration — defined near WM_CREATE.
static HFONT g_hMenuFont;
static bool  s_lineNumsOn = false; // persists across tabs; toggled by gutter click

// Returns the language index matching the given lowercase extension, or 0 (Plain Text).
static int Ne_LangFromExt(const std::wstring& path)
{
    size_t dot = path.rfind(L'.');
    if (dot == std::wstring::npos) return 0;
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    // Check filename-based matches (no dot in extension list means match full basename)
    std::wstring base = path.substr(path.find_last_of(L"\\/") + 1);
    std::wstring baseLow = base;
    for (auto& c : baseLow) c = (wchar_t)towlower(c);

    for (int i = 0; i < NE_LANG_COUNT; ++i) {
        const wchar_t* p = s_langs[i].exts;
        while (p && *p) {
            const wchar_t* e = wcschr(p, L'|');
            size_t len = e ? (size_t)(e - p) : wcslen(p);
            // Match against extension
            if (ext.size() == len && wcsncmp(ext.c_str(), p, len) == 0)
                return i;
            // Match against full basename (for Makefile, .editorconfig etc.)
            if (baseLow.size() == len && wcsncmp(baseLow.c_str(), p, len) == 0)
                return i;
            p = e ? e + 1 : nullptr;
        }
    }
    return 0; // Plain Text
}

// Apply a light-theme color scheme to a Scintilla window.
static void Ne_SetupScintillaStyle(HWND hSci)
{
    auto sci = [hSci](UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        return SendMessageW(hSci, msg, wp, lp);
    };
    // Set STYLE_DEFAULT then propagate to all styles
    sci(SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Consolas");
    sci(SCI_STYLESETSIZE, STYLE_DEFAULT, 10);
    sci(SCI_STYLESETBACK, STYLE_DEFAULT, RGB(255,255,255));
    sci(SCI_STYLESETFORE, STYLE_DEFAULT, RGB(0,0,0));
    sci(SCI_STYLECLEARALL, 0, 0);

    // Common style indices used across most Scintilla lexers
    // 1 = block comment,  2 = line comment,  3 = doc comment
    sci(SCI_STYLESETFORE, 1, RGB(0,128,0));
    sci(SCI_STYLESETFORE, 2, RGB(0,128,0));
    sci(SCI_STYLESETFORE, 3, RGB(0,128,128));
    // 4 = number,  5 = keyword1,  6 = string,  7 = char
    sci(SCI_STYLESETFORE, 4, RGB(9,136,90));
    sci(SCI_STYLESETFORE, 5, RGB(0,0,200));
    sci(SCI_STYLESETBOLD, 5, TRUE);
    sci(SCI_STYLESETFORE, 6, RGB(163,21,21));
    sci(SCI_STYLESETFORE, 7, RGB(163,21,21));
    // 8 = UUID/script,  9 = preprocessor,  10 = keyword2 / operator
    sci(SCI_STYLESETFORE, 9, RGB(128,64,0));
    sci(SCI_STYLESETFORE, 10, RGB(0,0,160));
    sci(SCI_STYLESETBOLD, 10, TRUE);

    // Editor-level settings
    sci(SCI_SETCARETFORE, RGB(0,0,0), 0);
    sci(SCI_SETSELBACK, TRUE, RGB(179,215,255));
    sci(SCI_SETSCROLLWIDTHTRACKING, TRUE, 0);
    sci(SCI_SETWRAPMODE, SC_WRAP_NONE, 0);
    sci(SCI_SETTABWIDTH, 4, 0);
    // Line number margin (margin 0)
    sci(SCI_SETMARGINTYPEN,  0, SC_MARGIN_NUMBER);
    sci(SCI_SETMARGINWIDTHN, 0, s_lineNumsOn ? S(44) : 0);
    sci(SCI_STYLESETBACK, STYLE_LINENUMBER, RGB(240,240,240));
    sci(SCI_STYLESETFORE, STYLE_LINENUMBER, RGB(100,100,100));
    sci(SCI_STYLESETSIZE, STYLE_LINENUMBER, 8);
    sci(SCI_STYLESETFONT, STYLE_LINENUMBER, (LPARAM)"Consolas");
    // Autocomplete settings
    sci(SCI_AUTOCSETIGNORECASE, TRUE, 0);
    sci(SCI_AUTOCSETAUTOHIDE,   TRUE, 0);
    sci(SCI_AUTOCSETDROPRESTOFWORD, FALSE, 0);
}

// Assign a Lexilla lexer (by name) to a Scintilla window.
static void Ne_SetScintillaLexer(HWND hSci, const char* lexerName)
{
    if (!lexerName) {
        SendMessageW(hSci, SCI_SETILEXER, 0, 0);
        return;
    }
    ILexer5* pLex = CreateLexer(lexerName);
    if (pLex)
        SendMessageW(hSci, SCI_SETILEXER, 0, (LPARAM)pLex);
}

// Forward declaration — defined after s_langKws[].
static void Ne_ApplyLang(HWND hSci, int langIdx);

// ── RichEdit line-number gutter ───────────────────────────────────────────────
// NsbLineGutter is a plain child window painted to show line numbers next to a
// RichEdit control.  The associated RichEdit HWND is stored in GWLP_USERDATA.

static LRESULT CALLBACK NsbLineGutterProc(HWND hWnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        HWND hEdit = (HWND)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        if (hEdit) SendMessageW(hEdit, msg, wParam, lParam);
        return 0;
    }

    case WM_SETCURSOR: {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
        RECT rc; GetClientRect(hWnd, &rc);
        bool thinStrip = (rc.right <= S(24));  // thin strip = fully clickable
        bool inBtn = true;
        if (!thinStrip) {  // full gutter: only top-left ◂ area is clickable
            int btnSz = S(18);
            RECT btnRc = { S(1), 0, S(1) + btnSz, btnSz };
            POINT mpt = { pt.x, pt.y };
            inBtn = (PtInRect(&btnRc, mpt) != FALSE);
        }
        SetCursor(LoadCursorW(NULL, inBtn ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }

    case WM_MOUSEMOVE: {
        HWND hTip = (HWND)GetPropW(hWnd, L"hTip");
        if (hTip) {
            MSG relayMsg = { hWnd, msg, wParam, lParam };
            SendMessageW(hTip, TTM_RELAYEVENT, 0, (LPARAM)&relayMsg);
        }
        if (!GetPropW(hWnd, L"hover")) {
            SetPropW(hWnd, L"hover", (HANDLE)1);
            InvalidateRect(hWnd, NULL, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        HWND hTip = (HWND)GetPropW(hWnd, L"hTip");
        if (hTip) {
            MSG relayMsg = { hWnd, msg, wParam, lParam };
            SendMessageW(hTip, TTM_RELAYEVENT, 0, (LPARAM)&relayMsg);
        }
        RemovePropW(hWnd, L"hover");
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hWnd, &rc);
        bool thinStrip = (rc.right <= S(24));
        bool inBtn = true;
        if (!thinStrip) {  // full gutter: only top-left ◂ area is clickable
            int btnSz = S(18);
            RECT btnRc = { S(1), 0, S(1) + btnSz, btnSz };
            POINT pt = { mx, my };
            inBtn = (PtInRect(&btnRc, pt) != FALSE);
        }
        if (inBtn)
            PostMessageW(GetParent(hWnd), WM_COMMAND,
                         MAKEWPARAM(IDC_BTN_LINENUM, BN_CLICKED), 0);
        return 0;
    }

    case WM_PAINT: {
        HWND hEdit = (HWND)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        bool hover = (GetPropW(hWnd, L"hover") != NULL);
        int btnSz = S(18);

        // Background
        HBRUSH hbr = CreateSolidBrush(RGB(240, 240, 240));
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);

        HFONT hf = g_hMenuFont;
        HFONT hOldFont = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
        SetBkMode(hdc, TRANSPARENT);

        // Thin strip: width <= S(24) means either numbers-off OR a Scintilla tab.
        // In both cases just show ▸ (off) or ◂ (on) — no line numbers drawn here.
        bool thinStrip = (rc.right <= S(24));

        if (thinStrip) {
            if (hover) {
                HBRUSH hHov = CreateSolidBrush(RGB(210, 230, 245));
                FillRect(hdc, &rc, hHov);
                DeleteObject(hHov);
            }
            const wchar_t* glyph = s_lineNumsOn ? L"\u25C2" : L"\u25B8";
            RECT rcBtn = { 0, S(2), rc.right, S(2) + btnSz };
            SetTextColor(hdc, RGB(60, 100, 140));
            DrawTextW(hdc, glyph, -1, &rcBtn,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        } else {
            // Full gutter (RichEdit, numbers ON): separator + numbers + ◂ header
            HPEN hpen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hpen);
            MoveToEx(hdc, rc.right - 1, rc.top, NULL);
            LineTo(hdc, rc.right - 1, rc.bottom);
            SelectObject(hdc, hOldPen);
            DeleteObject(hpen);

            // Line numbers — right-aligned
            if (hEdit) {
                SetTextColor(hdc, RGB(100, 100, 100));
                int firstLine  = (int)SendMessageW(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
                int totalLines = (int)SendMessageW(hEdit, EM_GETLINECOUNT,        0, 0);
                for (int ln = firstLine; ln < totalLines; ++ln) {
                    int charIdx = (int)SendMessageW(hEdit, EM_LINEINDEX, ln, 0);
                    if (charIdx < 0) break;
                    POINTL pt = {};
                    SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)charIdx);
                    int y = (int)pt.y;
                    if (y >= rc.bottom) break;
                    if (y < 0) continue;
                    wchar_t num[16];
                    swprintf_s(num, L"%d", ln + 1);
                    RECT rcNum = { 0, y, rc.right - S(4), y + S(20) };
                    DrawTextW(hdc, num, -1, &rcNum,
                              DT_RIGHT | DT_SINGLELINE | DT_TOP | DT_NOCLIP);
                }
            }

            // ◂ drawn last — always on top of the numbers
            RECT btnRc = { S(1), 0, S(1) + btnSz, btnSz };
            if (hover) {
                POINT mpt; GetCursorPos(&mpt); ScreenToClient(hWnd, &mpt);
                if (PtInRect(&btnRc, mpt)) {
                    HBRUSH hHov = CreateSolidBrush(RGB(210, 230, 245));
                    FillRect(hdc, &btnRc, hHov);
                    DeleteObject(hHov);
                }
            }
            SetTextColor(hdc, RGB(60, 100, 140));
            DrawTextW(hdc, L"\u25C2", -1, &btnRc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }
        if (hOldFont) SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Create the gutter for a RichEdit doc if not yet created.
static void Ne_EnsureLineGutter(HWND hwndParent, NeTabDoc* d)
{
    if (!d || !d->hEdit || d->hLineGutter) return;
    d->hLineGutter = CreateWindowExW(0, L"NsbLineGutter", L"",
        WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 1, 1,
        hwndParent, NULL, GetModuleHandleW(NULL), NULL);
    if (!d->hLineGutter) return;
    SetWindowLongPtrW(d->hLineGutter, GWLP_USERDATA, (LONG_PTR)d->hEdit);

    // Attach a tooltip for the toggle button
    HWND hTip = CreateWindowExW(0, TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        d->hLineGutter, NULL, GetModuleHandleW(NULL), NULL);
    if (hTip) {
        SetWindowPos(hTip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        TOOLINFOW ti = {};
        ti.cbSize   = sizeof(ti);
        ti.uFlags   = TTF_SUBCLASS;
        ti.hwnd     = d->hLineGutter;
        ti.uId      = 1;
        ti.rect     = { 0, 0, 9999, 9999 };
        ti.lpszText = const_cast<LPWSTR>(
            s_lineNumsOn ? L"Hide line numbers" : L"Show line numbers");
        SendMessageW(hTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
        SetPropW(d->hLineGutter, L"hTip", (HANDLE)hTip);
    }
}

// Reposition all gutters and shrink editor windows to make room.
// The gutter is always visible (as a thin strip) for both RichEdit and Scintilla tabs.
// Must be called after NeTabs_SetRects so the base edit rect is current.
static void Ne_SyncRichGutters(HWND hwnd)
{
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    int ex = st->editX, ey = st->editY, ew = st->editW, eh = st->editH;
    if (ew <= 0 || eh <= 0) return;

    int n = NeTabs_GetCount(hwnd);
    for (int i = 0; i < n; ++i) {
        NeTabDoc* d = NeTabs_GetDocByIndex(hwnd, i);
        if (!d || !d->hEdit) continue;
        Ne_EnsureLineGutter(hwnd, d);

        bool editVis = (IsWindowVisible(d->hEdit) != FALSE);
        bool sciVis  = (d->hSci && IsWindowVisible(d->hSci) != FALSE);
        bool vis     = editVis || sciVis;

        // Gutter width:
        //   Scintilla tab → always thin strip (S(20)); Scintilla draws its own numbers.
        //   RichEdit tab  → S(44) when numbers on, S(20) when off.
        int gutW = (sciVis || !s_lineNumsOn) ? S(20) : S(44);

        if (d->hLineGutter) {
            SetWindowPos(d->hLineGutter, HWND_TOP,
                         ex, ey, gutW, std::max(1, eh),
                         SWP_NOACTIVATE);
            ShowWindow(d->hLineGutter, vis ? SW_SHOWNOACTIVATE : SW_HIDE);
            if (vis) InvalidateRect(d->hLineGutter, NULL, TRUE);
        }

        int editLeft = ex + gutW;
        int editW    = std::max(1, ew - gutW);

        // Resize the visible editor (RichEdit or Scintilla) to sit beside the gutter.
        if (editVis)
            SetWindowPos(d->hEdit, NULL,
                         editLeft, ey, editW, std::max(1, eh),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        if (sciVis)
            SetWindowPos(d->hSci, NULL,
                         editLeft, ey, editW, std::max(1, eh),
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// Toggle line numbers on all open tabs and refresh all gutters.
static void Ne_SyncLineNumBtn(HWND hwnd)
{
    const wchar_t* tipText =
        s_lineNumsOn ? L"Hide line numbers" : L"Show line numbers";
    int n = NeTabs_GetCount(hwnd);
    for (int i = 0; i < n; ++i) {
        NeTabDoc* d = NeTabs_GetDocByIndex(hwnd, i);
        if (!d) continue;
        if (d->hSci)
            SendMessageW(d->hSci, SCI_SETMARGINWIDTHN, 0, s_lineNumsOn ? S(44) : 0);
        if (d->hLineGutter) {
            HWND hTip = (HWND)GetPropW(d->hLineGutter, L"hTip");
            if (hTip) {
                TOOLINFOW ti = {};
                ti.cbSize   = sizeof(ti);
                ti.hwnd     = d->hLineGutter;
                ti.uId      = 1;
                ti.lpszText = const_cast<LPWSTR>(tipText);
                SendMessageW(hTip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
            }
            InvalidateRect(d->hLineGutter, NULL, FALSE);
        }
    }
    Ne_SyncRichGutters(hwnd);
}

// Create and configure a Scintilla child window (hidden; caller shows it).
static HWND Ne_CreateScintilla(HWND hwndParent, int x, int y, int w, int h)
{
    HWND hSci = CreateWindowExW(0, L"Scintilla", L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, std::max(1,w), std::max(1,h),
        hwndParent, NULL, GetModuleHandleW(NULL), NULL);
    if (!hSci) return NULL;
    Ne_SetupScintillaStyle(hSci);
    // Enable save-point + caret notifications
    SendMessageW(hSci, SCI_SETMODEVENTMASK,
        SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT, 0);
    // SCN_UPDATEUI is always sent on selection/caret change — no extra mask needed.
    return hSci;
}

// Get UTF-8 text from a Scintilla window.
static std::string Ne_SciGetText(HWND hSci)
{
    int len = (int)SendMessageW(hSci, SCI_GETLENGTH, 0, 0);
    if (len <= 0) return {};
    std::string buf(len + 1, '\0');
    SendMessageW(hSci, SCI_GETTEXT, (WPARAM)(len + 1), (LPARAM)buf.data());
    buf.resize(len);
    return buf;
}

// ── Keyword autocomplete ──────────────────────────────────────────────────────
// Space-separated keyword lists, indexed by s_langs[] order.
// Empty string = no autocomplete for that language.
static const char* s_langKws[] = {
    /* 0 Plain Text  */ "",
    /* 1 Batch       */ "break call cd chdir cls color copy del dir do echo else endlocal errorlevel exit for goto if md mkdir move not pause pushd rem rd rmdir set setlocal shift start title type",
    /* 2 C / C++     */ "alignas alignof auto bool break case catch char char16_t char32_t char8_t class concept const const_cast consteval constexpr constinit continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false final float for friend goto if inline int long mutable namespace new noexcept nullptr operator override private protected public reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while",
    /* 3 C#          */ "abstract as base bool break byte case catch char checked class const continue decimal default delegate do double else enum event explicit extern false finally fixed float for foreach goto if implicit in int interface internal is lock long namespace new null object operator out override params private protected public readonly ref return sbyte sealed short sizeof stackalloc static string struct switch this throw true try typeof uint ulong unchecked unsafe ushort using virtual void volatile while",
    /* 4 CSS         */ "align-content align-items align-self animation background background-color background-image border border-radius bottom box-shadow color content display flex flex-direction float font font-family font-size font-weight gap grid height justify-content left letter-spacing line-height margin max-height max-width min-height min-width opacity overflow padding position right text-align text-decoration top transform transition visibility white-space width z-index",
    /* 5 HTML        */ "a abbr address area article aside audio b base blockquote body br button canvas caption cite code col colgroup data datalist dd details dfn dialog div dl dt em embed fieldset figcaption figure footer form h1 h2 h3 h4 h5 h6 head header hr html i iframe img input ins kbd label legend li link main map mark menu meta meter nav noscript object ol optgroup option output p picture pre progress q rp rt ruby s samp script section select small source span strong style sub summary sup table tbody td template textarea tfoot th thead time title tr track u ul var video wbr",
    /* 6 INI         */ "",
    /* 7 Java        */ "abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for goto if implements import instanceof int interface long native new null package private protected public return short static strictfp super switch synchronized this throw throws transient true try var void volatile while",
    /* 8 JavaScript  */ "async await break case catch class const continue debugger default delete do else export extends false finally for from function get if import in instanceof let new null of return set static super switch this throw true try typeof undefined var void while with yield",
    /* 9 JSON        */ "false null true",
    /* 10 Lua        */ "and break do else elseif end false for function goto if in local nil not or repeat return then true until while",
    /* 11 Makefile   */ "define else endef endif export ifdef ifndef ifeq ifneq include override private undefine unexport vpath",
    /* 12 Markdown   */ "",
    /* 13 Pascal     */ "and array as begin boolean break byte case char class const constructor continue default destructor div do downto else end except exit extended false file finalization finally for function goto if implementation in inherited initialization inline integer interface is label library longint mod nil not object of on or out overload override packed pointer procedure program property protected public published raise real record repeat result self set shortint shl shr single string then to true try type unit until uses var while with word xor",
    /* 14 Perl       */ "chomp chop chr close connect delete die do each else elsif eval exit exists for foreach format goto if index join keys last length local map my next no our package pop print printf push q qq qr qw qx ref return reverse scalar shift sort splice split sprintf sub substr system tie tied undef unless until use values wantarray while",
    /* 15 PHP        */ "abstract and array break callable case catch class clone const continue declare default die do echo else elseif empty enddeclare endfor endforeach endif endswitch endwhile extends final finally fn for foreach function global goto if implements include include_once instanceof insteadof interface isset list match namespace new null or print private protected public readonly require require_once return static switch throw trait try unset use var while xor yield",
    /* 16 PowerShell */ "begin break catch class continue data define do dynamicparam else elseif end exit filter finally for foreach from function if in param process return switch throw trap try until using while",
    /* 17 Python     */ "False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda match nonlocal not or pass raise return try while with yield",
    /* 18 Ruby       */ "alias and begin break case class def defined do else elsif end ensure false for if in module next nil not or redo rescue retry return self super then true undef unless until when while yield",
    /* 19 Rust       */ "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self static struct super trait true type union unsafe use where while",
    /* 20 SQL        */ "add all alter and any as asc authorization backup begin between break by cascade case check close clustered coalesce column commit constraint contains continue convert create cross current cursor database declare default delete deny desc distinct do drop else end exec execute exists exit fetch for foreign from full function goto grant group having identity if index inner insert intersect into is join key left like load merge national not null nullif of off on open option or order outer over percent pivot primary print proc procedure public raiserror read reconfigure references replication restore return revoke right rollback rowcount rule save schema select set statistics table then to top tran transaction trigger truncate union unique unpivot update use user values view when where while with",
    /* 21 TypeScript */ "abstract any as async at await boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object of out override private protected public readonly return set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield",
    /* 22 VBScript   */ "and as boolean byref byval call case class const date dim do each else elseif empty end enum erase error event exit explicit false for function get if imp in is let like loop me mod new next not nothing null object on option or preserve private property public randomize redim rem resume select set step string sub then to true type until wend while with",
    /* 23 XML        */ "",
    /* 24 YAML       */ "false null true",
};
static_assert(sizeof(s_langKws)/sizeof(s_langKws[0]) == NE_LANG_COUNT,
              "s_langKws length must match NE_LANG_COUNT");

// Apply lexer + keywords + per-lexer style colours to a Scintilla window.
static void Ne_ApplyLang(HWND hSci, int langIdx)
{
    if (langIdx < 0 || langIdx >= NE_LANG_COUNT) {
        SendMessageW(hSci, SCI_SETILEXER, 0, 0);
        return;
    }
    const char* lexerName = s_langs[langIdx].lexer;
    Ne_SetScintillaLexer(hSci, lexerName);

    // Send keyword list so the lexer colours keywords.
    const char* kws = s_langKws[langIdx];
    if (kws && *kws)
        SendMessageA(hSci, SCI_SETKEYWORDS, 0, (LPARAM)kws);

    if (!lexerName) return;
    auto sc = [hSci](UINT m, WPARAM w, LPARAM l){ SendMessageW(hSci, m, w, l); };

    if (strcmp(lexerName, "phpscript") == 0) {
        // SCE_HPHP_*: 118=default 119=dqstring 120=sqstring 121=keyword
        //             122=number  123=$variable 124=/*comment*/ 125=//comment
        //             126=${var}-in-string  127=operator
        sc(SCI_STYLESETFORE, 119, RGB(163,21,21));
        sc(SCI_STYLESETFORE, 120, RGB(163,21,21));
        sc(SCI_STYLESETFORE, 121, RGB(0,0,200));   sc(SCI_STYLESETBOLD, 121, TRUE);
        sc(SCI_STYLESETFORE, 122, RGB(9,136,90));
        sc(SCI_STYLESETFORE, 123, RGB(128,0,128));
        sc(SCI_STYLESETFORE, 124, RGB(0,128,0));   sc(SCI_STYLESETITALIC, 124, TRUE);
        sc(SCI_STYLESETFORE, 125, RGB(0,128,0));   sc(SCI_STYLESETITALIC, 125, TRUE);
        sc(SCI_STYLESETFORE, 126, RGB(163,21,21));
    } else if (strcmp(lexerName, "hypertext") == 0) {
        // SCE_H_*: 1=tag 2=unknowntag 3=attr 4=unknownattr 5=number
        //          6=dqstring 7=sqstring 9=comment 10=entity
        sc(SCI_STYLESETFORE, 1,  RGB(86,156,214));
        sc(SCI_STYLESETFORE, 2,  RGB(86,156,214));
        sc(SCI_STYLESETFORE, 3,  RGB(156,220,254));
        sc(SCI_STYLESETFORE, 4,  RGB(156,220,254));
        sc(SCI_STYLESETFORE, 5,  RGB(9,136,90));
        sc(SCI_STYLESETFORE, 6,  RGB(163,21,21));
        sc(SCI_STYLESETFORE, 7,  RGB(163,21,21));
        sc(SCI_STYLESETFORE, 9,  RGB(0,128,0));  sc(SCI_STYLESETITALIC, 9, TRUE);
        sc(SCI_STYLESETFORE, 10, RGB(163,100,0));
    }
}

// Show Scintilla keyword autocomplete for the current word prefix.
static void Ne_SciAutoComplete(HWND hSci, int langIdx)
{
    if (langIdx < 0 || langIdx >= NE_LANG_COUNT) return;
    const char* kws = s_langKws[langIdx];
    if (!kws || !*kws) return;

    Sci_Position pos       = (Sci_Position)SendMessageW(hSci, SCI_GETCURRENTPOS,      0,    0);
    Sci_Position wordStart = (Sci_Position)SendMessageW(hSci, SCI_WORDSTARTPOSITION, (WPARAM)pos, TRUE);
    int prefixLen = (int)(pos - wordStart);
    if (prefixLen < 2) { SendMessageW(hSci, SCI_AUTOCCANCEL, 0, 0); return; }

    // Extract the typed prefix
    std::string prefix(prefixLen + 1, '\0');
    Sci_TextRange tr{};
    tr.chrg.cpMin = (Sci_PositionCR)wordStart;
    tr.chrg.cpMax = (Sci_PositionCR)pos;
    tr.lpstrText  = prefix.data();
    SendMessageW(hSci, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
    prefix.resize(prefixLen);

    // Collect keywords that start with the typed prefix
    std::vector<std::string> matches;
    const char* p = kws;
    while (*p) {
        while (*p == ' ') ++p;
        const char* end = p;
        while (*end && *end != ' ') ++end;
        if (end > p) {
            size_t kwLen = (size_t)(end - p);
            if (kwLen >= (size_t)prefixLen &&
                _strnicmp(p, prefix.c_str(), (size_t)prefixLen) == 0)
                matches.emplace_back(p, kwLen);
        }
        p = end;
    }
    if (matches.empty()) { SendMessageW(hSci, SCI_AUTOCCANCEL, 0, 0); return; }

    // Dismiss if the only match is the exact word already typed
    if (matches.size() == 1 && _stricmp(matches[0].c_str(), prefix.c_str()) == 0) {
        SendMessageW(hSci, SCI_AUTOCCANCEL, 0, 0); return;
    }

    std::sort(matches.begin(), matches.end(),
        [](const std::string& a, const std::string& b){ return _stricmp(a.c_str(), b.c_str()) < 0; });

    std::string list;
    for (auto& m : matches) { if (!list.empty()) list += ' '; list += m; }

    SendMessageW(hSci, SCI_AUTOCSHOW, (WPARAM)prefixLen, (LPARAM)list.c_str());
}

// Update the Language menu checkmarks to reflect the active tab's langId.
static void Ne_UpdateLangMenuCheck(int langId)
{
    if (!s_hLangMenu) return;
    if (langId < 0 || langId >= NE_LANG_COUNT) {
        // Uncheck all
        for (int i = 0; i < NE_LANG_COUNT; ++i)
            CheckMenuItem(s_hLangMenu, IDM_LANG_BASE + i, MF_BYCOMMAND | MF_UNCHECKED);
        return;
    }
    CheckMenuRadioItem(s_hLangMenu,
        IDM_LANG_BASE, IDM_LANG_BASE + NE_LANG_COUNT - 1,
        IDM_LANG_BASE + langId,
        MF_BYCOMMAND);
}

// ── Owner-draw menu support ───────────────────────────────────────────────────
// g_hMenuFont declared near top of file (needs to be visible to NsbLineGutterProc).
static HICON g_hFtpMenuIcon = NULL;

struct NeMenuItemData {
    std::wstring   text;         // owned copy — never dangles
    bool           isSeparator;
    bool           isBar;
    HICON          hSmallIcon;
};
static std::vector<NeMenuItemData*> g_menuItemStorage;

static void Ne_AppendMenuOD(HMENU hMenu, UINT flags, UINT_PTR id, const wchar_t* text, bool isBar = false, HICON hIcon = NULL)
{
    bool sep = (flags & MF_SEPARATOR) != 0;
    NeMenuItemData* d = new NeMenuItemData{ text ? text : L"", sep, isBar, hIcon };
    g_menuItemStorage.push_back(d);
    AppendMenuW(hMenu, flags | MF_OWNERDRAW, id, (LPCWSTR)d);
}

// ── RichEdit DLL ───────────────────────────────────────────────────────────────
static HMODULE s_neRtfDll = NULL;

// ── Global main hwnd (for message-loop keyboard intercept) ─────────────────────
static HWND s_hwndMain = NULL;

// ── Locale / i18n ─────────────────────────────────────────────────────────────
static std::unordered_map<std::wstring, std::wstring> g_str;

static std::wstring Ne_Unescape(const std::wstring& s)
{
    std::wstring r; r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            ++i;
            switch (s[i]) {
                case L'n': r += L'\r'; r += L'\n'; break;
                case L't': r += L'\t'; break;
                default:   r += s[i]; break;
            }
        } else r += s[i];
    }
    return r;
}

static void Ne_LoadLocale()
{
    // Loads the embedded locale resource (RCDATA id IDR_LOCALE_EN_GB).
    // Add locale-selection logic here when more locales are added.
    HINSTANCE hi = GetModuleHandleW(NULL);
    HRSRC hRes = FindResourceW(hi, MAKEINTRESOURCEW(IDR_LOCALE_EN_GB), RT_RCDATA);
    if (!hRes) return;
    HGLOBAL hG = LoadResource(hi, hRes);
    if (!hG) return;
    const char* data = (const char*)LockResource(hG);
    DWORD sz = SizeofResource(hi, hRes);
    if (!data || !sz) return;
    int wn = MultiByteToWideChar(CP_UTF8, 0, data, (int)sz, NULL, 0);
    std::wstring text(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, (int)sz, text.data(), wn);
    std::wstringstream ss(text);
    std::wstring line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        while (!key.empty() && key.back() == L' ') key.pop_back();
        if (!val.empty() && val.front() == L' ') val.erase(0, 1);
        g_str[std::move(key)] = Ne_Unescape(val);
    }
}

// Returns the localized string for key, or key itself as fallback.
static const wchar_t* Ls(const wchar_t* key)
{
    auto it = g_str.find(key);
    return (it != g_str.end()) ? it->second.c_str() : key;
}

// Builds a double-null-terminated filter string from a locale key using '|' as pair separator.
static std::wstring Ne_Filter(const wchar_t* key)
{
    std::wstring s = Ls(key);
    for (auto& c : s) if (c == L'|') c = L'\0';
    s += L'\0'; // c_str() adds the second \0 → OPENFILENAME double-null terminator
    return s;
}

// ── RTF stream helpers ─────────────────────────────────────────────────────────
struct NeStreamBuf { const std::string* src; size_t pos; };

static DWORD CALLBACK Ne_ReadCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    NeStreamBuf* rb = (NeStreamBuf*)cookie;
    size_t rem = rb->src->size() - rb->pos;
    LONG   n   = (LONG)(rem < (size_t)cb ? rem : (size_t)cb);
    if (n > 0) { memcpy(buf, rb->src->c_str() + rb->pos, n); rb->pos += n; }
    *pcb = n;
    return 0;
}

static DWORD CALLBACK Ne_WriteCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    std::string* s = (std::string*)cookie;
    s->append((char*)buf, cb);
    *pcb = cb;
    return 0;
}

static void Ne_StreamIn(HWND hEdit, const std::string& bytes, bool asRtf)
{
    NeStreamBuf rb = { &bytes, 0 };
    EDITSTREAM  es = { (DWORD_PTR)&rb, 0, Ne_ReadCb };
    SendMessageW(hEdit, EM_STREAMIN, asRtf ? SF_RTF : (SF_TEXT | SF_UNICODE), (LPARAM)&es);
}

static std::string Ne_StreamOut(HWND hEdit, bool asRtf)
{
    std::string s;
    EDITSTREAM  es = { (DWORD_PTR)&s, 0, Ne_WriteCb };
    SendMessageW(hEdit, EM_STREAMOUT, asRtf ? SF_RTF : (SF_TEXT | SF_UNICODE), (LPARAM)&es);
    return s;
}

// ── Character format helpers ───────────────────────────────────────────────────
static void Ne_ToggleEffect(HWND hEdit, DWORD maskBit, DWORD effectBit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = maskBit | CFM_COLOR;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if (cf.dwEffects & effectBit) cf.dwEffects &= ~effectBit;
    else                          cf.dwEffects |=  effectBit;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void Ne_ToggleScript(HWND hEdit, DWORD wantBit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SUBSCRIPT | CFM_COLOR;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool wasSet = (cf.dwEffects & wantBit) != 0;
    cf.dwEffects &= ~(CFE_SUBSCRIPT | CFE_SUPERSCRIPT);
    if (!wasSet) cf.dwEffects |= wantBit;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void Ne_SetAlignment(HWND hEdit, WORD align)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask     = PFM_ALIGNMENT;
    pf.wAlignment = align;
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void Ne_ToggleBullet(HWND hEdit)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    pf.dwMask     = PFM_NUMBERING | PFM_OFFSET;
    if (pf.wNumbering == PFN_BULLET) { pf.wNumbering = 0;          pf.dxOffset = 0;   }
    else                              { pf.wNumbering = PFN_BULLET;  pf.dxOffset = 360; }
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void Ne_ToggleNumbered(HWND hEdit)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    pf.dwMask          = PFM_NUMBERING | PFM_OFFSET | PFM_NUMBERINGSTYLE | PFM_NUMBERINGSTART;
    if (pf.wNumbering == PFN_ARABIC) {
        pf.wNumbering = 0;          pf.dxOffset        = 0;
        pf.wNumberingStyle = 0;     pf.wNumberingStart  = 1;
    } else {
        pf.wNumbering      = PFN_ARABIC;    pf.dxOffset        = 360;
        pf.wNumberingStyle = PFNS_PERIOD;   pf.wNumberingStart = 1;
    }
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

// ── Indent / Outdent ──────────────────────────────────────────────────────────
// ── Per-button text colours ───────────────────────────────────────────────────
static COLORREF Ne_BtnTextColor(int id)
{
    switch (id) {
    // Character formatting
    case IDC_NE_BOLD:        return RGB(15,  30,  140); // deep navy
    case IDC_NE_ITALIC:      return RGB(30,  85,  200); // royal blue
    case IDC_NE_UNDERLINE:   return RGB(0,   110, 215); // cobalt blue
    case IDC_NE_STRIKE:      return RGB(180, 25,  25);  // crimson
    case IDC_NE_SUBSCRIPT:   return RGB(0,   135, 115); // teal
    case IDC_NE_SUPERSCRIPT: return RGB(0,   135, 115); // teal
    // Alignment
    case IDC_NE_ALIGN_L:
    case IDC_NE_ALIGN_C:
    case IDC_NE_ALIGN_R:
    case IDC_NE_ALIGN_J:     return RGB(55,  75,  170); // slate blue
    // Lists
    case IDC_NE_BULLET:
    case IDC_NE_NUMBERED:    return RGB(20,  130, 30);  // forest green
    // Colour pickers
    case IDC_NE_COLOR:       return RGB(200, 15,  15);  // red — classic "A" in Word
    // Indent
    case IDC_NE_INDENT_IN:
    case IDC_NE_INDENT_OUT:  return RGB(50,  100, 175); // steel blue
    // Spacing
    case IDC_NE_LINESPACE:   return RGB(125, 0,   185); // violet
    case IDC_NE_PARSPACE:    return RGB(125, 0,   185); // violet
    // Tools
    case IDC_NE_FIND:        return RGB(195, 95,  0);   // amber
    case IDC_NE_LINK:        return RGB(0,   80,  215); // hyperlink blue
    case IDC_NE_TABLE:       return RGB(0,   120, 105); // dark teal
    case IDC_NE_HLINE:       return RGB(100, 100, 110); // slate grey
    case IDC_NE_CLEARFMT:    return RGB(175, 0,   0);   // deep red
    case IDC_NE_PRINT_BTN:   return RGB(0,   120, 0);   // forest green
    case IDC_NE_IMAGE:       return RGB(55,  95,  185); // blue
    // View / misc
    case IDC_NE_WORDWRAP:    return RGB(0,   140, 105); // teal green
    case IDC_NE_CASE:        return RGB(105, 0,   165); // purple
    case IDC_BTN_LINENUM:    return RGB(60,  120, 160); // steel blue
    default:                 return RGB(30,  30,  30);  // near-black
    }
}

static void Ne_Indent(HWND hEdit, bool increase)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_OFFSET | PFM_STARTINDENT;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    LONG step = 360; // 0.25 inch in twips
    LONG newOff = pf.dxOffset + (increase ? step : -step);
    LONG newSt  = pf.dxStartIndent + (increase ? step : -step);
    if (newOff < 0) newOff = 0;
    if (newSt  < 0) newSt  = 0;
    pf.dwMask        = PFM_OFFSET | PFM_STARTINDENT;
    pf.dxOffset      = newOff;
    pf.dxStartIndent = newSt;
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

// ── Clear all character formatting on selection ───────────────────────────────
static void Ne_ClearFormatting(HWND hEdit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask    = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT |
                   CFM_SUBSCRIPT | CFM_SUPERSCRIPT | CFM_COLOR | CFM_BACKCOLOR |
                   CFM_SIZE | CFM_FACE | CFM_CHARSET;
    cf.dwEffects = CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR;
    cf.yHeight   = 240; // 12pt default
    cf.bCharSet  = DEFAULT_CHARSET;
    wcsncpy_s(cf.szFaceName, L"Segoe UI", LF_FACESIZE - 1);
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// ── Horizontal rule helpers — implementations are after Ne_ShowHRulePropsDialog ─
static bool Ne_CaretOnHRule(HWND hEdit)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_BORDER | PFM_STYLE;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    // any stored border value marks an HR paragraph
    return pf.sStyle == 42 || pf.wBorders != 0;
}
static void Ne_InsertHRule(HWND hwnd, HWND hEdit);    // defined after Ne_ShowHRulePropsDialog
static void Ne_EditHRuleProps(HWND hwnd, HWND hEdit);  // defined after Ne_ShowHRulePropsDialog
static void Ne_RebuildHRList(HWND hEdit);              // defined near Ne_PaintHRules

// ── Toggle word wrap (wrap to window ↔ no wrap) ───────────────────────────────
static bool s_wordWrapOn  = true;
static void Ne_ToggleWordWrap(HWND hwnd, HWND hEdit)
{
    s_wordWrapOn = !s_wordWrapOn;
    // EM_SETTARGETDEVICE: 0 = wrap to window, large value = no wrap
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, s_wordWrapOn ? 0 : 30000);
    // Sync button state
    HWND hBtn = GetDlgItem(hwnd, IDC_NE_WORDWRAP);
    if (hBtn) SendMessageW(hBtn, BM_SETCHECK, s_wordWrapOn ? BST_CHECKED : BST_UNCHECKED, 0);
}

// ── Toggle character case: UPPER → lower → Title → UPPER ─────────────────────
static void Ne_ToggleCase(HWND hEdit)
{
    CHARRANGE cr = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMin == cr.cpMax) return; // nothing selected

    int len = cr.cpMax - cr.cpMin;
    std::wstring buf(len + 1, L'\0');
    SendMessageW(hEdit, EM_GETSELTEXT, 0, (LPARAM)buf.data());
    buf.resize(len);

    // Detect current state: all upper → go lower; all lower → go Title; else → UPPER
    bool allUpper = true, allLower = true;
    for (wchar_t c : buf) {
        if (isalpha((unsigned)c)) {
            if (iswlower(c)) allUpper = false;
            if (iswupper(c)) allLower = false;
        }
    }

    std::wstring out = buf;
    if (allUpper) {
        // UPPER → lower
        for (auto& c : out) c = towlower(c);
    } else if (allLower) {
        // lower → Title
        bool capNext = true;
        for (auto& c : out) {
            if (iswspace(c)) { capNext = true; }
            else if (capNext && iswalpha(c)) { c = towupper(c); capNext = false; }
            else capNext = false;
        }
    } else {
        // mixed → UPPER
        for (auto& c : out) c = towupper(c);
    }

    // Replace selection with transformed text (preserve formatting).
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)out.c_str());
    // Restore selection span.
    cr.cpMax = cr.cpMin + (LONG)out.size();
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
}

// ── Image insertion (PNG / JPEG → \pict hex in RTF stream) ────────────────────
enum class NeImgFmt { Unknown, PNG, JPEG };
struct NeImgInfo { NeImgFmt fmt; int w, h; };

static NeImgInfo Ne_ClassifyImage(const std::vector<unsigned char>& d)
{
    NeImgInfo info = { NeImgFmt::Unknown, 0, 0 };
    if (d.size() < 24) return info;
    if (d[0]==0x89 && d[1]==0x50 && d[2]==0x4E && d[3]==0x47 &&
        d[4]==0x0D && d[5]==0x0A && d[6]==0x1A && d[7]==0x0A) {
        info.fmt = NeImgFmt::PNG;
        info.w = (d[16]<<24)|(d[17]<<16)|(d[18]<<8)|d[19];
        info.h = (d[20]<<24)|(d[21]<<16)|(d[22]<<8)|d[23];
        return info;
    }
    if (d[0]==0xFF && d[1]==0xD8) {
        info.fmt = NeImgFmt::JPEG;
        size_t i = 2;
        while (i + 8 < d.size()) {
            if (d[i] != 0xFF) break;
            unsigned char m = d[i+1];
            if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
                info.h = (d[i+5]<<8)|d[i+6];
                info.w = (d[i+7]<<8)|d[i+8];
                return info;
            }
            if (m == 0xD9 || m == 0xD8 || m == 0x01) break;
            int segLen = (d[i+2]<<8)|d[i+3];
            i += 2 + (size_t)segLen;
        }
        return info;
    }
    return info;
}

static void Ne_InsertImage(HWND hwnd, HWND hEdit)
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    auto filtImg = Ne_Filter(L"FILTER_IMAGE");
    ofn.lpstrFilter = filtImg.c_str();
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = Ls(L"DLG_IMAGE");
    if (!GetOpenFileNameW(&ofn)) return;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) {
        MessageBoxW(hwnd, Ls(L"MSG_IMG_OPEN_ERR"), Ls(L"DLG_IMAGE"), MB_OK | MB_ICONERROR);
        return;
    }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> raw((size_t)fsz);
    fread(raw.data(), 1, (size_t)fsz, f);
    fclose(f);

    NeImgInfo info = Ne_ClassifyImage(raw);
    if (info.fmt == NeImgFmt::Unknown || info.w <= 0 || info.h <= 0) {
        MessageBoxW(hwnd, Ls(L"MSG_IMG_FMT_ERR"), Ls(L"DLG_IMAGE"), MB_OK | MB_ICONWARNING);
        return;
    }

    int goalW = info.w * 15;
    int goalH = info.h * 15;
    if (goalW > 5760) { goalH = (int)((long long)5760 * info.h / info.w); goalW = 5760; }
    if (goalH > 11520) { goalW = (int)((long long)11520 * info.w / info.h); goalH = 11520; }

    static const char hx[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(raw.size() * 2 + raw.size() / 64 + 2);
    for (size_t i = 0; i < raw.size(); i++) {
        hex += hx[(raw[i] >> 4) & 0xF];
        hex += hx[raw[i] & 0xF];
        if ((i + 1) % 64 == 0) hex += '\n';
    }

    const char* blip = (info.fmt == NeImgFmt::PNG) ? "\\pngblip" : "\\jpegblip";
    std::string snippet = "{\\rtf1\\ansi {\\pict";
    snippet += blip;
    snippet += "\\picw"     + std::to_string(info.w);
    snippet += "\\pich"     + std::to_string(info.h);
    snippet += "\\picwgoal" + std::to_string(goalW);
    snippet += "\\pichgoal" + std::to_string(goalH);
    snippet += "\r\n" + hex + "}}";

    NeStreamBuf rb = { &snippet, 0 };
    EDITSTREAM  es = { (DWORD_PTR)&rb, 0, Ne_ReadCb };
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
    SetFocus(hEdit);
}

// ── Font sizes ─────────────────────────────────────────────────────────────────
static const int s_neFontSizes[] = { 8,9,10,11,12,14,16,18,20,22,24,28,32,36,48,72 };
static const int s_neFontCount   = (int)(sizeof(s_neFontSizes) / sizeof(s_neFontSizes[0]));
static const int s_neFontDefault = 4; // index of 12 pt

static int CALLBACK Ne_FontEnumProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lp)
{
    if (lf->lfFaceName[0] == L'@') return 1;
    std::vector<std::wstring>* fonts = (std::vector<std::wstring>*)lp;
    for (auto& f : *fonts) if (_wcsicmp(f.c_str(), lf->lfFaceName) == 0) return 1;
    fonts->push_back(lf->lfFaceName);
    return 1;
}

// ── Toolbar sync ───────────────────────────────────────────────────────────────
static void Ne_SyncToolbar(HWND hwnd, HWND hEdit)
{
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->updatingToolbar) return;
    st->updatingToolbar = true;

    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT |
                CFM_SUBSCRIPT | CFM_FACE | CFM_SIZE;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    HWND hFace = GetDlgItem(hwnd, IDC_NE_FONTFACE);
    if (hFace && cf.szFaceName[0]) {
        int cnt = (int)SendMessageW(hFace, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < cnt; i++) {
            wchar_t buf[LF_FACESIZE] = {};
            SendMessageW(hFace, CB_GETLBTEXT, i, (LPARAM)buf);
            if (_wcsicmp(buf, cf.szFaceName) == 0) { SendMessageW(hFace, CB_SETCURSEL, (WPARAM)i, 0); break; }
        }
    }

    HWND hSzC = GetDlgItem(hwnd, IDC_NE_FONTSIZE);
    if (hSzC && cf.yHeight > 0) {
        int pt = cf.yHeight / 20;
        int cnt = (int)SendMessageW(hSzC, CB_GETCOUNT, 0, 0);
        int best = 0, bestDiff = INT_MAX;
        for (int i = 0; i < cnt; i++) {
            wchar_t buf[16] = {};
            SendMessageW(hSzC, CB_GETLBTEXT, i, (LPARAM)buf);
            int diff = abs(_wtoi(buf) - pt);
            if (diff < bestDiff) { bestDiff = diff; best = i; }
        }
        SendMessageW(hSzC, CB_SETCURSEL, (WPARAM)best, 0);
    }

    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT | PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);

    auto setCheck = [&](int id, bool on) {
        HWND h = GetDlgItem(hwnd, id);
        if (h) SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    };

    setCheck(IDC_NE_BOLD,        (cf.dwEffects & CFE_BOLD)        != 0);
    setCheck(IDC_NE_ITALIC,      (cf.dwEffects & CFE_ITALIC)      != 0);
    setCheck(IDC_NE_UNDERLINE,   (cf.dwEffects & CFE_UNDERLINE)   != 0);
    setCheck(IDC_NE_STRIKE,      (cf.dwEffects & CFE_STRIKEOUT)   != 0);
    setCheck(IDC_NE_SUBSCRIPT,   (cf.dwEffects & CFE_SUBSCRIPT)   != 0);
    setCheck(IDC_NE_SUPERSCRIPT, (cf.dwEffects & CFE_SUPERSCRIPT) != 0);

    setCheck(IDC_NE_ALIGN_L, pf.wAlignment == PFA_LEFT || pf.wAlignment == 0);
    setCheck(IDC_NE_ALIGN_C, pf.wAlignment == PFA_CENTER);
    setCheck(IDC_NE_ALIGN_R, pf.wAlignment == PFA_RIGHT);
    setCheck(IDC_NE_ALIGN_J, pf.wAlignment == PFA_JUSTIFY);
    setCheck(IDC_NE_BULLET,   pf.wNumbering == PFN_BULLET);
    setCheck(IDC_NE_NUMBERED, pf.wNumbering == PFN_ARABIC);

    st->updatingToolbar = false;
}

// ── Zoom helpers ──────────────────────────────────────────────────────────────
static int g_zoomRtf = 100; // RTF: percentage (50/75/100/125/150/200)
static int g_zoomSci = 0;   // Scintilla: point offset (-10..+20)

static const int s_zoomSteps[]   = { 50, 75, 100, 125, 150, 200 };
static const int s_zoomStepCount = (int)(sizeof(s_zoomSteps)/sizeof(s_zoomSteps[0]));

// Set the zoom combobox to the nearest preset step for RTF %.
static void Ne_SyncZoomCombo(HWND hwnd, int pct)
{
    HWND hZ = GetDlgItem(hwnd, IDC_NE_ZOOM);
    if (!hZ) return;
    int best = 0, bestDiff = INT_MAX;
    for (int i = 0; i < s_zoomStepCount; ++i) {
        int d = abs(s_zoomSteps[i] - pct);
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    SendMessageW(hZ, CB_SETCURSEL, (WPARAM)best, 0);
}

// Apply saved zoom to the given RTF or Scintilla window.
static void Ne_ApplyZoomToDoc(NeTabDoc* doc)
{
    if (!doc) return;
    if (doc->hSci) {
        SendMessageW(doc->hSci, SCI_SETZOOM, (WPARAM)g_zoomSci, 0);
    } else if (doc->hEdit) {
        SendMessageW(doc->hEdit, EM_SETZOOM, (WPARAM)g_zoomRtf, 100);
    }
}

// Step zoom up (+1), down (-1), or reset (0) for the active tab. Saves to DB.
static void Ne_StepZoom(HWND hwnd, int dir)
{
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (!doc) return;

    if (doc->hSci) {
        if      (dir > 0)  SendMessageW(doc->hSci, SCI_ZOOMIN,  0, 0);
        else if (dir < 0)  SendMessageW(doc->hSci, SCI_ZOOMOUT, 0, 0);
        else               SendMessageW(doc->hSci, SCI_SETZOOM, 0, 0);
        g_zoomSci = (int)SendMessageW(doc->hSci, SCI_GETZOOM, 0, 0);
        NeProfiles_SetIntSetting("zoom_sci", g_zoomSci);
    } else if (doc->hEdit) {
        // Find current step index, then move.
        int cur = g_zoomRtf;
        int idx = 2; // default = 100%
        for (int i = 0; i < s_zoomStepCount; ++i)
            if (s_zoomSteps[i] == cur) { idx = i; break; }
        if      (dir > 0 && idx < s_zoomStepCount - 1) ++idx;
        else if (dir < 0 && idx > 0)                   --idx;
        else if (dir == 0)                              idx = 2; // 100%
        g_zoomRtf = s_zoomSteps[idx];
        SendMessageW(doc->hEdit, EM_SETZOOM, (WPARAM)g_zoomRtf, 100);
        Ne_SyncZoomCombo(hwnd, g_zoomRtf);
        NeProfiles_SetIntSetting("zoom_rtf", g_zoomRtf);
    }
}

// ── Title and status bar ───────────────────────────────────────────────────────
// Forward declarations — defined later after NeEncoding helpers.
static const wchar_t* Ne_EncLabel(NeEncoding enc);
static bool           Ne_DocIsRtf(NeTabDoc* doc);
static void Ne_UpdateTitle(HWND hwnd)
{
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (!doc) return;
    std::wstring title;
    if (doc->modified) title += L"* ";
    if (doc->path.empty()) {
        title += Ls(L"UNTITLED");
        title += L" \u2014 NSBEdit";
    } else {
        size_t pos = doc->path.find_last_of(L"\\/");
        title += (pos == std::wstring::npos ? doc->path : doc->path.substr(pos + 1));
        title += L" \u2014 NSBEdit";
    }
    SetWindowTextW(hwnd, title.c_str());
}

static void Ne_UpdateStatusText(HWND hwnd)
{
    HWND hSb = GetDlgItem(hwnd, IDC_NE_STATUSBAR);
    if (!hSb) return;
    NeTabDoc* doc  = NeTabs_GetActiveDoc(hwnd);

    int words = 0, chars = 0;
    if (doc && doc->hSci) {
        // Count from Scintilla (UTF-8 byte length ≈ char count for status bar)
        int byteLen = (int)SendMessageW(doc->hSci, SCI_GETLENGTH, 0, 0);
        chars = byteLen;
        if (byteLen > 0) {
            std::string utf8 = Ne_SciGetText(doc->hSci);
            bool inWord = false;
            for (char ch : utf8) {
                bool ws = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
                if (!ws && !inWord) { ++words; inWord = true; }
                else if (ws) inWord = false;
            }
        }
    } else {
        HWND hEdit = doc ? doc->hEdit : NULL;
        if (hEdit) {
            int len = GetWindowTextLengthW(hEdit);
            chars = len;
            if (len > 0) {
                std::vector<wchar_t> buf((size_t)len + 1);
                GetWindowTextW(hEdit, buf.data(), len + 1);
                bool inWord = false;
                for (int i = 0; i < len; ++i) {
                    wchar_t c = buf[i];
                    bool ws = (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r');
                    if (!ws && !inWord) { ++words; inWord = true; }
                    else if (ws) inWord = false;
                }
            }
        }
    }
    bool modified = doc ? doc->modified : false;
    NeStatusBar_Update(hSb, words, chars, modified);

    // Line / Col from caret position
    int caretLine = 0, caretCol = 0;
    if (doc && doc->hSci) {
        int pos  = (int)SendMessageW(doc->hSci, SCI_GETCURRENTPOS, 0, 0);
        caretLine = (int)SendMessageW(doc->hSci, SCI_LINEFROMPOSITION, (WPARAM)pos, 0) + 1;
        caretCol  = (int)SendMessageW(doc->hSci, SCI_GETCOLUMN, (WPARAM)pos, 0) + 1;
    } else if (doc && doc->hEdit) {
        DWORD selStart = 0;
        SendMessageW(doc->hEdit, EM_GETSEL, (WPARAM)&selStart, 0);
        int lineIdx = (int)SendMessageW(doc->hEdit, EM_LINEFROMCHAR, (WPARAM)selStart, 0);
        int lineStart = (int)SendMessageW(doc->hEdit, EM_LINEINDEX, (WPARAM)lineIdx, 0);
        caretLine = lineIdx + 1;
        caretCol  = (int)(selStart - (DWORD)lineStart) + 1;
    }
    NeStatusBar_SetLineCol(hSb, caretLine, caretCol);

    // Centre info: encoding / language
    NeEncoding enc = doc ? (NeEncoding)doc->encoding : NeEncoding::Unknown;
    if (doc && doc->hSci && doc->langId >= 0 && doc->langId < NE_LANG_COUNT)
        NeStatusBar_SetInfo(hSb, s_langs[doc->langId].name);
    else
        NeStatusBar_SetInfo(hSb, Ne_EncLabel(enc));
}

enum class NeBtnTone { Blue, Green, Red };

struct NeDialogButtonSpec {
    int id;
    std::wstring text;
    NeBtnTone tone;
    const wchar_t* iconRes;
    int width;
    HICON hIconOverride = NULL;  // if set, used instead of iconRes
};

struct NeDialogData {
    std::wstring title;
    std::wstring message;
    int textH = 0;
    int result = IDCANCEL;
    int closeResult = IDCANCEL;
    int buttonCount = 0;
    NeDialogButtonSpec buttons[4];
    HICON hMsgIcon = NULL;   // optional left-side icon (e.g. IDI_WARNING)
    HFONT hDlgFont = NULL;   // created in WM_CREATE, deleted in WM_NCDESTROY
};

static bool Ne_PromptSaveIfModified(HWND hwnd); // forward
static bool Ne_LoadPathIntoEditor(HWND hwnd, const std::wstring& path);

static bool Ne_GetFileStamp(const std::wstring& path, FILETIME* outWrite, ULONGLONG* outSize)
{
    if (!outWrite || !outSize || path.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return false;
    *outWrite = fa.ftLastWriteTime;
    *outSize = ((ULONGLONG)fa.nFileSizeHigh << 32) | (ULONGLONG)fa.nFileSizeLow;
    return true;
}

static void Ne_RememberDiskStamp(NeTabDoc* doc)
{
    if (!doc || doc->path.empty()) {
        if (doc) doc->hasDiskStamp = false;
        return;
    }
    FILETIME ft = {};
    ULONGLONG sz = 0;
    if (Ne_GetFileStamp(doc->path, &ft, &sz)) {
        doc->diskWriteTime = ft;
        doc->diskFileSize = sz;
        doc->hasDiskStamp = true;
    } else {
        doc->hasDiskStamp = false;
    }
}

static HFONT Ne_CreateDialogFont(bool bold)
{
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfHeight = -MulDiv(12, GetDpiForSystem(), 72);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    if (bold) ncm.lfMessageFont.lfWeight = FW_BOLD;
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

static int Ne_MeasureDialogTextHeight(const std::wstring& text, int maxW)
{
    HDC hdc = GetDC(NULL);
    if (!hdc) return S(48);
    HFONT hf = Ne_CreateDialogFont(false);
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
    RECT rc = { 0, 0, maxW, 0 };
    DrawTextW(hdc, text.c_str(), -1, &rc,
              DT_WORDBREAK | DT_CENTER | DT_CALCRECT | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
    if (hf) DeleteObject(hf);
    ReleaseDC(NULL, hdc);
    int h = rc.bottom - rc.top;
    return std::max(h, S(24));
}

static int Ne_MeasureButtonWidth(const std::wstring& text)
{
    HDC hdc = GetDC(NULL);
    if (!hdc) return S(120);
    HFONT hf = Ne_CreateDialogFont(true);
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    if (old) SelectObject(hdc, old);
    if (hf) DeleteObject(hf);
    ReleaseDC(NULL, hdc);
    // icon + gap + text + horizontal padding
    int w = S(16) + S(8) + sz.cx + S(24);
    return std::max(w, S(120));
}

static COLORREF Ne_ToneColor(NeBtnTone tone, bool pressed, bool hover)
{
    switch (tone) {
        case NeBtnTone::Green:
            return pressed ? RGB(100, 160, 100) : hover ? RGB(155, 205, 155) : RGB(175, 215, 175);
        case NeBtnTone::Red:
            return pressed ? RGB(190, 100, 100) : hover ? RGB(225, 145, 145) : RGB(235, 175, 175);
        default:
            return pressed ? RGB(204, 228, 247) : hover ? RGB(229, 241, 251) : RGB(225, 225, 225);
    }
}

static int Ne_ButtonIndexById(const NeDialogData* dd, int id)
{
    if (!dd) return -1;
    for (int i = 0; i < dd->buttonCount; ++i)
        if (dd->buttons[i].id == id) return i;
    return -1;
}

static void Ne_DrawDialogButton(const DRAWITEMSTRUCT* dis, const NeDialogData* dd)
{
    if (!dis || !dd) return;
    int idx = Ne_ButtonIndexById(dd, dis->CtlID);
    if (idx < 0) return;
    const NeDialogButtonSpec& b = dd->buttons[idx];

    RECT rc = dis->rcItem;
    HDC hdc = dis->hDC;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool hover   = (GetPropW(dis->hwndItem, L"NeHover") != NULL);

    HBRUSH hb = CreateSolidBrush(Ne_ToneColor(b.tone, pressed, hover));
    FillRect(hdc, &rc, hb);
    DeleteObject(hb);
    FrameRect(hdc, &rc, GetSysColorBrush(COLOR_3DSHADOW));

    HFONT hf = Ne_CreateDialogFont(true);
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;

    SIZE ts = {};
    GetTextExtentPoint32W(hdc, b.text.c_str(), (int)b.text.size(), &ts);
    int contentW = S(16) + S(8) + ts.cx;
    int startX = rc.left + ((rc.right - rc.left) - contentW) / 2;
    int iconY = rc.top + ((rc.bottom - rc.top) - S(16)) / 2;

    HICON hIcon = b.hIconOverride
        ? b.hIconOverride
        : LoadIconW(NULL, b.iconRes ? b.iconRes : IDI_INFORMATION);
    if (hIcon) DrawIconEx(hdc, startX, iconY, hIcon, S(16), S(16), 0, NULL, DI_NORMAL);

    RECT tr = rc;
    tr.left = startX + S(16) + S(8);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(25, 25, 25));
    DrawTextW(hdc, b.text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

    if (dis->itemState & ODS_FOCUS) {
        RECT fr = rc;
        InflateRect(&fr, -S(4), -S(4));
        DrawFocusRect(hdc, &fr);
    }

    if (old) SelectObject(hdc, old);
    if (hf) DeleteObject(hf);
}

static LRESULT CALLBACK Ne_BtnHoverProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC prev = (WNDPROC)GetPropW(hwnd, L"NePrevProc");
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!GetPropW(hwnd, L"NeHover")) {
            SetPropW(hwnd, L"NeHover", (HANDLE)1);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropW(hwnd, L"NeHover");
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_NCDESTROY:
        RemovePropW(hwnd, L"NeHover");
        RemovePropW(hwnd, L"NePrevProc");
        break;
    }
    return prev ? CallWindowProcW(prev, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK Ne_DialogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NeDialogData* dd = (NeDialogData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        dd = (NeDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dd);

        HINSTANCE hi = GetModuleHandleW(NULL);
        HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
        if (hIco) {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIco);
        }

        dd->hDlgFont = Ne_CreateDialogFont(false);

        RECT rc = {}; GetClientRect(hwnd, &rc);
        const int padH = S(20), padT = S(18), padB = S(15), gapTB = S(14), btnH = S(34), btnGap = S(10);

        // Optional left-side icon (warning / info)
        int iconW = 0;
        if (dd->hMsgIcon) {
            iconW = S(32) + S(12);
            HWND hIcoCtrl = CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
                padH, padT, S(32), S(32),
                hwnd, NULL, hi, NULL);
            if (hIcoCtrl) SendMessageW(hIcoCtrl, STM_SETICON, (WPARAM)dd->hMsgIcon, 0);
        }

        HWND hTxt = CreateWindowExW(0, L"STATIC", dd->message.c_str(),
            WS_CHILD | WS_VISIBLE | (dd->hMsgIcon ? SS_LEFT : SS_CENTER),
            padH + iconW, padT, rc.right - 2 * padH - iconW, dd->textH,
            hwnd, (HMENU)(UINT_PTR)IDC_NE_DLG_TEXT, hi, NULL);
        if (hTxt) SendMessageW(hTxt, WM_SETFONT, (WPARAM)dd->hDlgFont, TRUE);

        int totalW = 0;
        for (int i = 0; i < dd->buttonCount; ++i) {
            totalW += dd->buttons[i].width;
            if (i + 1 < dd->buttonCount) totalW += btnGap;
        }
        int bx = (rc.right - totalW) / 2;
        int by = rc.bottom - padB - btnH;
        for (int i = 0; i < dd->buttonCount; ++i) {
            DWORD style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
            if (i == 0) style |= BS_DEFPUSHBUTTON;
            HWND hBtn = CreateWindowExW(0, L"BUTTON", dd->buttons[i].text.c_str(),
                style, bx, by, dd->buttons[i].width, btnH,
                hwnd, (HMENU)(UINT_PTR)dd->buttons[i].id, hi, NULL);
            if (hBtn) {
                SendMessageW(hBtn, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                // Subclass for hover tracking
                WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
                SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
            }
            bx += dd->buttons[i].width + btnGap;
        }
        return 0;
    }
    case WM_NCDESTROY:
        if (dd && dd->hDlgFont) { DeleteObject(dd->hDlgFont); dd->hDlgFont = NULL; }
        break;
    case WM_DRAWITEM:
        Ne_DrawDialogButton((const DRAWITEMSTRUCT*)lParam, dd);
        return TRUE;
    case WM_COMMAND:
        if (!dd) break;
        if (HIWORD(wParam) == BN_CLICKED) {
            int id = LOWORD(wParam);
            if (Ne_ButtonIndexById(dd, id) >= 0) {
                dd->result = id;
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_CLOSE:
        if (dd) dd->result = dd->closeResult;
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, RGB(20, 20, 20));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int Ne_ShowChoiceDialog(HWND parent, const wchar_t* title, const std::wstring& message,
                               NeDialogButtonSpec* buttons, int buttonCount, int closeResult,
                               HICON hMsgIcon = NULL)
{
    if (buttonCount <= 0 || buttonCount > 3) return closeResult;

    for (int i = 0; i < buttonCount; ++i)
        buttons[i].width = Ne_MeasureButtonWidth(buttons[i].text);

    const int padH = S(20), padT = S(18), padB = S(15), gapTB = S(14), btnH = S(34), btnGap = S(10);
    int contW = S(420);
    int textH = Ne_MeasureDialogTextHeight(message, contW);
    int totalBtnW = 0;
    for (int i = 0; i < buttonCount; ++i) {
        totalBtnW += buttons[i].width;
        if (i + 1 < buttonCount) totalBtnW += btnGap;
    }
    int clientW = std::max(contW + 2 * padH, totalBtnW + 2 * padH);
    int clientH = padT + textH + gapTB + btnH + padB;

    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    RECT pr = {};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - winW) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - winH) / 2;

    RECT wa = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    if (x + winW > wa.right) x = wa.right - winW;
    if (y + winH > wa.bottom) y = wa.bottom - winH;

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = Ne_DialogWndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"NSBEditChoiceDialogClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) RegisterClassW(&wc);

    NeDialogData dd = {};
    dd.title = title ? title : L"";
    dd.message = message;
    dd.textH = textH;
    dd.closeResult = closeResult;
    dd.result = closeResult;
    dd.buttonCount = buttonCount;
    for (int i = 0; i < buttonCount; ++i) dd.buttons[i] = buttons[i];
    dd.hMsgIcon = hMsgIcon;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME,
        wc.lpszClassName, dd.title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, winW, winH, parent, NULL, hi, &dd);
    if (!dlg) return closeResult;

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    MSG m;
    while (IsWindow(dlg) && GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    return dd.result;
}

static bool Ne_CheckExternalFileChangeOnFocus(HWND hwnd)
{
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (!doc || doc->path.empty()) return true;

    FILETIME ft = {};
    ULONGLONG sz = 0;
    if (!Ne_GetFileStamp(doc->path, &ft, &sz)) {
        doc->hasDiskStamp = false;
        return true;
    }
    if (!doc->hasDiskStamp) {
        doc->diskWriteTime = ft;
        doc->diskFileSize = sz;
        doc->hasDiskStamp = true;
        return true;
    }

    bool changed = (CompareFileTime(&ft, &doc->diskWriteTime) != 0) || (sz != doc->diskFileSize);
    if (!changed) return true;

    wchar_t msg[MAX_PATH + 256] = {};
    swprintf_s(msg, Ls(L"MSG_FILE_CHANGED_PROMPT"), doc->path.c_str());

    NeDialogButtonSpec btns[2] = {
        { IDYES, Ls(L"BTN_RELOAD"), NeBtnTone::Green, IDI_INFORMATION, 0 },
        { IDNO,  Ls(L"BTN_KEEP"),   NeBtnTone::Blue,  IDI_WARNING,     0 },
    };
    int r = Ne_ShowChoiceDialog(hwnd, Ls(L"DLG_FILE_CHANGED"), msg, btns, 2, IDNO);
    if (r == IDYES) {
        if (!Ne_LoadPathIntoEditor(hwnd, doc->path)) {
            MessageBoxW(hwnd, Ls(L"MSG_OPEN_ERR"), Ls(L"APP_NAME"), MB_OK | MB_ICONERROR);
            Ne_RememberDiskStamp(doc);
            return false;
        }
        return true;
    }

    doc->diskWriteTime = ft;
    doc->diskFileSize = sz;
    doc->hasDiskStamp = true;
    return true;
}

// ── File operations ────────────────────────────────────────────────────────────

static void Ne_Print(HWND hwnd)
{
    HWND hEdit = NeTabs_GetActiveEdit(hwnd);
    if (!hEdit) return;

    PRINTDLGW pd = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner   = hwnd;
    pd.Flags       = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    if (!PrintDlgW(&pd)) return;   // user cancelled

    HDC hDC = pd.hDC;
    if (pd.hDevMode) { GlobalFree(pd.hDevMode); pd.hDevMode = NULL; }
    if (pd.hDevNames) { GlobalFree(pd.hDevNames); pd.hDevNames = NULL; }

    // Page size in twips (1 inch = 1440 twips).
    int pageW = MulDiv(GetDeviceCaps(hDC, PHYSICALWIDTH),
                       1440, GetDeviceCaps(hDC, LOGPIXELSX));
    int pageH = MulDiv(GetDeviceCaps(hDC, PHYSICALHEIGHT),
                       1440, GetDeviceCaps(hDC, LOGPIXELSY));
    // Printable area offset (margins built into printer).
    int offX  = MulDiv(GetDeviceCaps(hDC, PHYSICALOFFSETX),
                       1440, GetDeviceCaps(hDC, LOGPIXELSX));
    int offY  = MulDiv(GetDeviceCaps(hDC, PHYSICALOFFSETY),
                       1440, GetDeviceCaps(hDC, LOGPIXELSY));
    // Printable rect in twips.
    RECT rcPrint;
    rcPrint.left   = offX;
    rcPrint.top    = offY;
    rcPrint.right  = pageW  - offX;
    rcPrint.bottom = pageH - offY;

    // Get document character range.
    GETTEXTLENGTHEX gtl = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
    int docLen = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);

    DOCINFOW di = {};
    di.cbSize   = sizeof(di);
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    std::wstring docTitle = (doc && !doc->path.empty()) ? doc->path : Ls(L"UNTITLED");
    di.lpszDocName = docTitle.c_str();

    if (StartDocW(hDC, &di) <= 0) { DeleteDC(hDC); return; }

    CHARRANGE crSaved = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&crSaved);

    FORMATRANGE fr = {};
    fr.hdc       = hDC;
    fr.hdcTarget = hDC;
    fr.rc        = rcPrint;
    fr.rcPage    = rcPrint;
    fr.chrg.cpMin = 0;
    fr.chrg.cpMax = -1;

    int printed = 0;
    while (printed < docLen) {
        StartPage(hDC);
        fr.chrg.cpMin = printed;
        fr.chrg.cpMax = -1;
        printed = (int)SendMessageW(hEdit, EM_FORMATRANGE, TRUE, (LPARAM)&fr);
        EndPage(hDC);
        if (printed <= fr.chrg.cpMin) break; // safety — no progress
        fr.rc = rcPrint;
    }

    // Free cached render info.
    SendMessageW(hEdit, EM_FORMATRANGE, FALSE, 0);
    EndDoc(hDC);
    DeleteDC(hDC);

    // Restore selection.
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crSaved);
}

// ── Export to PDF via "Microsoft Print to PDF" ──────────────────────────────
// We locate the printer by name, build a DEVMODE for it, then render the
// RichEdit content via EM_FORMATRANGE into a printer DC that writes a PDF
// file.  The output path is chosen by a Save As dialog — the user never
// sees a printer dialog.
static void Ne_ExportPdf(HWND hwnd)
{
    HWND hEdit = NeTabs_GetActiveEdit(hwnd);
    if (!hEdit) return;

    // ── Ask user where to save ────────────────────────────────────────────────
    wchar_t path[MAX_PATH] = {};
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (doc && !doc->path.empty()) {
        // Pre-fill with current filename, extension changed to .pdf
        wcsncpy_s(path, doc->path.c_str(), _TRUNCATE);
        wchar_t* dot = wcsrchr(path, L'.');
        if (dot) wcscpy_s(dot, MAX_PATH - (dot - path), L".pdf");
    }

    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    auto filtPdf    = Ne_Filter(L"FILTER_PDF");
    ofn.lpstrFilter = filtPdf.c_str();
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = Ls(L"DLG_EXPORT_PDF");
    if (!GetSaveFileNameW(&ofn)) return;

    // ── Open a printer DC for "Microsoft Print to PDF" ────────────────────────
    // We must set the output file in the DEVMODE before creating the DC.
    const wchar_t* printerName = L"Microsoft Print to PDF";
    HANDLE hPrinter = NULL;
    if (!OpenPrinterW((LPWSTR)printerName, &hPrinter, NULL)) {
        MessageBoxW(hwnd, Ls(L"MSG_PDF_ERR"), Ls(L"APP_NAME"), MB_OK | MB_ICONERROR);
        return;
    }

    LONG dmSize = DocumentPropertiesW(hwnd, hPrinter, (LPWSTR)printerName, NULL, NULL, 0);
    if (dmSize <= 0) { ClosePrinter(hPrinter); MessageBoxW(hwnd, Ls(L"MSG_PDF_ERR"), Ls(L"APP_NAME"), MB_OK | MB_ICONERROR); return; }

    std::vector<BYTE> dmBuf((size_t)dmSize);
    DEVMODEW* pDm = (DEVMODEW*)dmBuf.data();
    DocumentPropertiesW(hwnd, hPrinter, (LPWSTR)printerName, pDm, NULL, DM_OUT_BUFFER);
    ClosePrinter(hPrinter);

    // Set the output file name into DEVMODE.
    pDm->dmFields |= DM_PRINTQUALITY;
    pDm->dmPrintQuality = DMRES_HIGH;
    // The output filename is passed via DEVMODE.dmFields / private data on
    // "Microsoft Print to PDF" — we use the documented approach:
    // set dmOutputFile (not in standard DEVMODE, use SetPrinterData workaround
    // via CreateDCW with the path as the port name in a temporary copy).
    // Simplest portable way: set DM_ORIENTATION so DC is created, then
    // use DOCINFO.lpszOutput to redirect to file.
    pDm->dmFields      |= DM_ORIENTATION;
    pDm->dmOrientation  = DMORIENT_PORTRAIT;

    HDC hDC = CreateDCW(L"WINSPOOL", printerName, NULL, pDm);
    if (!hDC) {
        MessageBoxW(hwnd, Ls(L"MSG_PDF_ERR"), Ls(L"APP_NAME"), MB_OK | MB_ICONERROR);
        return;
    }

    // ── Start document, redirecting output to the chosen file ─────────────────
    NeTabDoc* docTitleSrc = NeTabs_GetActiveDoc(hwnd);
    std::wstring docTitle = (docTitleSrc && !docTitleSrc->path.empty())
                            ? docTitleSrc->path : Ls(L"UNTITLED");
    DOCINFOW di = {};
    di.cbSize      = sizeof(di);
    di.lpszDocName = docTitle.c_str();
    di.lpszOutput  = path;   // redirect PDF output to chosen file

    if (StartDocW(hDC, &di) <= 0) { DeleteDC(hDC); MessageBoxW(hwnd, Ls(L"MSG_PDF_ERR"), Ls(L"APP_NAME"), MB_OK | MB_ICONERROR); return; }

    // ── Page geometry in twips ────────────────────────────────────────────────
    int pageW = MulDiv(GetDeviceCaps(hDC, PHYSICALWIDTH),  1440, GetDeviceCaps(hDC, LOGPIXELSX));
    int pageH = MulDiv(GetDeviceCaps(hDC, PHYSICALHEIGHT), 1440, GetDeviceCaps(hDC, LOGPIXELSY));
    int offX  = MulDiv(GetDeviceCaps(hDC, PHYSICALOFFSETX), 1440, GetDeviceCaps(hDC, LOGPIXELSX));
    int offY  = MulDiv(GetDeviceCaps(hDC, PHYSICALOFFSETY), 1440, GetDeviceCaps(hDC, LOGPIXELSY));
    RECT rcPage = { offX, offY, pageW - offX, pageH - offY };

    GETTEXTLENGTHEX gtl = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
    int docLen = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);

    CHARRANGE crSaved = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&crSaved);

    FORMATRANGE fr = {};
    fr.hdc = fr.hdcTarget = hDC;
    fr.rc = fr.rcPage = rcPage;
    fr.chrg.cpMin = 0;
    fr.chrg.cpMax = -1;

    int printed = 0;
    while (printed < docLen) {
        StartPage(hDC);
        fr.chrg.cpMin = printed;
        fr.chrg.cpMax = -1;
        fr.rc = rcPage;
        printed = (int)SendMessageW(hEdit, EM_FORMATRANGE, TRUE, (LPARAM)&fr);
        EndPage(hDC);
        if (printed <= fr.chrg.cpMin) break;
    }

    SendMessageW(hEdit, EM_FORMATRANGE, FALSE, 0);  // free cached render info
    EndDoc(hDC);
    DeleteDC(hDC);

    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crSaved);
}

// ── Keyboard Shortcuts dialog ─────────────────────────────────────────────────
// Displays a read-only ListView with three columns: Shortcut | Function | Description.
// Sorted most-used → least-used.  Includes menu accelerators, Alt-key mnemonics,
// and standard RichEdit editing shortcuts so new users can discover everything.
struct ShortcutRow { const wchar_t* key; const wchar_t* func; const wchar_t* desc; };

static const ShortcutRow s_shortcuts[] = {
    // ── Formatting (most typed by far) ────────────────────────────────────────
    { L"[Ctrl]+B",             L"Bold",               L"Toggle bold on the selection" },
    { L"[Ctrl]+I",             L"Italic",             L"Toggle italic on the selection" },
    { L"[Ctrl]+U",             L"Underline",          L"Toggle underline on the selection" },
    // ── Edit ──────────────────────────────────────────────────────────────────
    { L"[Ctrl]+Z",             L"Undo",               L"Undo the last change" },
    { L"[Ctrl]+Y",             L"Redo",               L"Redo the last undone change" },
    { L"[Ctrl]+X",             L"Cut",                L"Cut selection to clipboard" },
    { L"[Ctrl]+C",             L"Copy",               L"Copy selection to clipboard" },
    { L"[Ctrl]+V",             L"Paste",              L"Paste from clipboard" },
    { L"[Ctrl]+A",             L"Select All",         L"Select the entire document" },
    // ── File ──────────────────────────────────────────────────────────────────
    { L"[Ctrl]+S",             L"Save",               L"Save the current file" },
    { L"[Ctrl]+[Shift]+S",     L"Save As",            L"Save to a new file" },
    { L"[Ctrl]+N",             L"New",                L"Create a new empty document" },
    { L"[Ctrl]+O",             L"Open",               L"Open a file" },
    { L"[Ctrl]+P",             L"Print",              L"Print the document" },
    { L"[Ctrl]+[Shift]+P",     L"Export as PDF",      L"Export the document as a PDF file" },
    // ── Navigation ────────────────────────────────────────────────────────────
    { L"[Ctrl]+[Home]",        L"Go to start",        L"Move cursor to start of document" },
    { L"[Ctrl]+[End]",         L"Go to end",          L"Move cursor to end of document" },
    { L"[Ctrl]+[Left]",        L"Word left",          L"Move cursor one word to the left" },
    { L"[Ctrl]+[Right]",       L"Word right",         L"Move cursor one word to the right" },
    { L"[Shift]+[Left/Right]", L"Extend selection",   L"Extend text selection one character" },
    { L"[Ctrl]+[Shift]+[Left/Right]", L"Select word", L"Extend selection one word at a time" },
    // ── Zoom ──────────────────────────────────────────────────────────────────
    { L"[Ctrl]+[+]",           L"Zoom in",            L"Increase zoom level" },
    { L"[Ctrl]+[-]",           L"Zoom out",           L"Decrease zoom level" },
    { L"[Ctrl]+0",             L"Reset zoom",         L"Reset zoom to 100% / default" },
    { L"[Ctrl]+[Scroll Up]",   L"Zoom in",            L"Increase zoom level" },
    { L"[Ctrl]+[Scroll Down]", L"Zoom out",           L"Decrease zoom level" },
    // ── Menu access (Alt mnemonics) ───────────────────────────────────────────
    { L"[Alt]+F, N",           L"New",                L"File menu \u2192 New" },
    { L"[Alt]+F, O",           L"Open",               L"File menu \u2192 Open" },
    { L"[Alt]+F, S",           L"Save",               L"File menu \u2192 Save" },
    { L"[Alt]+F, A",           L"Save As",            L"File menu \u2192 Save As" },
    { L"[Alt]+F, P",           L"Print",              L"File menu \u2192 Print" },
    { L"[Alt]+F, E",           L"Export as PDF",      L"File menu \u2192 Export as PDF" },
    { L"[Ctrl]+W",             L"Exit",               L"Close the document (exit app)" },
    { L"[Alt]+F, X",           L"Exit",               L"File menu \u2192 Exit" },
    { L"[Alt]+E, U",           L"Undo",               L"Edit menu \u2192 Undo" },
    { L"[Alt]+E, R",           L"Redo",               L"Edit menu \u2192 Redo" },
    { L"[Alt]+E, T",           L"Cut",                L"Edit menu \u2192 Cut" },
    { L"[Alt]+E, C",           L"Copy",               L"Edit menu \u2192 Copy" },
    { L"[Alt]+E, P",           L"Paste",              L"Edit menu \u2192 Paste" },
    { L"[Alt]+E, A",           L"Select All",         L"Edit menu \u2192 Select All" },
    { L"[Alt]+H, K",           L"Keyboard Shortcuts", L"Help menu \u2192 Keyboard Shortcuts" },
    { L"[Alt]+H, A",           L"About",              L"Help menu \u2192 About NSBEdit" },
    // ── Misc ──────────────────────────────────────────────────────────────────
    { L"[F1]",                 L"Keyboard Shortcuts", L"Show this keyboard shortcuts list" },
    { L"[Esc]",                L"Close dialog",       L"Close the current dialog" },
    { L"[Enter]",              L"New line",            L"Insert a new paragraph" },
    { L"[Tab]",                L"Indent",              L"Insert a tab character" },
    { L"[Delete]",             L"Delete",              L"Delete the character to the right" },
    { L"[Backspace]",          L"Backspace",           L"Delete the character to the left" },
    { L"[Ctrl]+[Delete]",      L"Delete word right",  L"Delete word to the right of cursor" },
    { L"[Ctrl]+[Backspace]",   L"Delete word left",   L"Delete word to the left of cursor" },
};
static const int s_shortcutCount = (int)(sizeof(s_shortcuts) / sizeof(s_shortcuts[0]));

// ── Shared helper: create a 12pt Segoe UI dialog font ─────────────────────────
static HFONT Ne_MakeDlgFont(HWND hwnd, bool bold = false)
{
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfHeight  = -MulDiv(12, GetDpiForWindow(hwnd) > 0 ? GetDpiForWindow(hwnd) : GetDpiForSystem(), 72);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    if (bold) ncm.lfMessageFont.lfWeight = FW_BOLD;
    return CreateFontIndirectW(&ncm.lfMessageFont);
}
// Apply font to all child controls of dlg.
static void Ne_ApplyDlgFont(HWND dlg, HFONT hf)
{
    EnumChildWindows(dlg, [](HWND h, LPARAM lp) -> BOOL {
        SendMessageW(h, WM_SETFONT, lp, TRUE);
        return TRUE;
    }, (LPARAM)hf);
}

// ── Find / Replace dialog (modeless) ─────────────────────────────────────────
static HWND s_hwndFind = NULL;
static HWND s_hwndFindEdit = NULL;  // the hEdit to search in (updated when opened)
static NeHighlightState s_findHL;   // current search highlights

// Cached search parameters — used to detect when a full rescan is needed.
static std::wstring s_findCachedNeedle;
static bool         s_findCachedMatchCase = false;
static bool         s_findCachedWholeWord = false;
static bool         s_findCachedRegex     = false;
static std::vector<NeHighlightRange> s_findMatches;  // all match ranges

static void Ne_UpdateFindCount()
{
    if (!s_hwndFind || !IsWindow(s_hwndFind)) return;
    HWND hCount = GetDlgItem(s_hwndFind, IDC_NE_DLG_FIND_COUNT);
    if (!hCount) return;
    if (s_findMatches.empty()) {
        SetWindowTextW(hCount, L"");
    } else {
        wchar_t buf[32];
        swprintf_s(buf, L"%d / %d",
                   s_findHL.activeIdx + 1, (int)s_findMatches.size());
        SetWindowTextW(hCount, buf);
    }
}

// Extract the full plain text from a RichEdit control.
static std::wstring Ne_GetEditText(HWND hEdit)
{
    GETTEXTLENGTHEX gle = { GTL_DEFAULT, 1200 };
    int len = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gle, 0);
    if (len <= 0) return {};
    std::wstring buf(len + 1, L'\0');
    GETTEXTEX gte = {};
    gte.cb       = (DWORD)((len + 1) * sizeof(wchar_t));
    gte.flags    = GT_DEFAULT;
    gte.codepage = 1200;   // Unicode
    int actual = (int)SendMessageW(hEdit, EM_GETTEXTEX, (WPARAM)&gte, (LPARAM)buf.data());
    buf.resize(actual);
    return buf;
}

// Case-insensitive wstring search helper.
static int Ne_FindInText(const std::wstring& text, const std::wstring& needle,
                         int startPos, bool forward, bool matchCase, bool wholeWord)
{
    if (needle.empty() || (int)text.size() < (int)needle.size()) return -1;
    const int tLen = (int)text.size();
    const int nLen = (int)needle.size();

    auto charMatch = [&](int ti) -> bool {
        if (!matchCase)
            return _wcsnicmp(text.c_str() + ti, needle.c_str(), nLen) == 0;
        return wcsncmp(text.c_str() + ti, needle.c_str(), nLen) == 0;
    };
    auto isWordChar = [](wchar_t c) -> bool {
        return iswalnum(c) || c == L'_';
    };

    if (forward) {
        for (int i = startPos; i <= tLen - nLen; i++) {
            if (!charMatch(i)) continue;
            if (wholeWord) {
                if (i > 0 && isWordChar(text[i - 1])) continue;
                if (i + nLen < tLen && isWordChar(text[i + nLen])) continue;
            }
            return i;
        }
    } else {
        for (int i = std::min(startPos, tLen - nLen); i >= 0; i--) {
            if (!charMatch(i)) continue;
            if (wholeWord) {
                if (i > 0 && isWordChar(text[i - 1])) continue;
                if (i + nLen < tLen && isWordChar(text[i + nLen])) continue;
            }
            return i;
        }
    }
    return -1;
}

static void Ne_DoFindNext(HWND dlg, bool forward = true)
{
    HWND hWhat = GetDlgItem(dlg, IDC_NE_DLG_FIND_WHAT);
    HWND hEdit = s_hwndFindEdit;
    if (!hWhat || !hEdit) return;

    wchar_t buf[512] = {};
    GetWindowTextW(hWhat, buf, 512);
    if (!buf[0]) {
        NeHighlight_Clear(hEdit, s_findHL);
        s_findMatches.clear();
        s_findCachedNeedle.clear();
        Ne_UpdateFindCount();
        return;
    }
    std::wstring needle(buf);

    bool matchCase = (SendMessageW(GetDlgItem(dlg, IDC_NE_DLG_MATCHCASE), BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool wholeWord = (SendMessageW(GetDlgItem(dlg, IDC_NE_DLG_WHOLEWORD), BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool useRegex  = (SendMessageW(GetDlgItem(dlg, IDC_NE_DLG_REGEX),     BM_GETCHECK, 0, 0) == BST_CHECKED);

    bool dirty = (needle    != s_findCachedNeedle    ||
                  matchCase != s_findCachedMatchCase ||
                  wholeWord != s_findCachedWholeWord ||
                  useRegex  != s_findCachedRegex);

    if (dirty) {
        NeHighlight_Clear(hEdit, s_findHL);
        s_findMatches.clear();

        std::wstring text = Ne_GetEditText(hEdit);

        if (useRegex) {
            std::wregex re;
            try {
                auto flags = std::regex_constants::ECMAScript;
                if (!matchCase) flags |= std::regex_constants::icase;
                re = std::wregex(needle, flags);
            } catch (...) {
                MessageBoxW(dlg, L"Invalid regular expression.", Ls(L"DLG_FIND"), MB_OK | MB_ICONERROR);
                Ne_UpdateFindCount();
                return;
            }
            for (auto it = std::wsregex_iterator(text.begin(), text.end(), re);
                 it != std::wsregex_iterator(); ++it) {
                int s = (int)it->position();
                s_findMatches.push_back({ s, s + (int)it->length() });
            }
        } else {
            int from = 0;
            while (true) {
                int pos = Ne_FindInText(text, needle, from, true, matchCase, wholeWord);
                if (pos < 0) break;
                s_findMatches.push_back({ pos, pos + (int)needle.size() });
                from = pos + (int)needle.size();
            }
        }

        s_findCachedNeedle    = needle;
        s_findCachedMatchCase = matchCase;
        s_findCachedWholeWord = wholeWord;
        s_findCachedRegex     = useRegex;

        if (s_findMatches.empty()) {
            Ne_UpdateFindCount();
            MessageBoxW(dlg, Ls(L"MSG_FIND_NOT_FOUND"), Ls(L"DLG_FIND"), MB_OK | MB_ICONINFORMATION);
            return;
        }

        // Pick nearest match to the current caret.
        CHARRANGE cr = {};
        SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
        int caretPos = forward ? (int)cr.cpMax : (int)cr.cpMin;
        int bestIdx  = forward ? 0 : (int)s_findMatches.size() - 1;
        if (forward) {
            for (int i = 0; i < (int)s_findMatches.size(); i++) {
                if (s_findMatches[i].start >= caretPos) { bestIdx = i; break; }
            }
        } else {
            for (int i = (int)s_findMatches.size() - 1; i >= 0; i--) {
                if (s_findMatches[i].start < caretPos) { bestIdx = i; break; }
            }
        }

        NeHighlight_SetAll(hEdit, s_findMatches, bestIdx,
                           NE_HL_FG, NE_HL_BG, NE_HL_BG_INACTIVE, s_findHL);
    } else {
        if (s_findMatches.empty()) return;
        int n = (int)s_findMatches.size();
        int newIdx = forward
            ? (s_findHL.activeIdx + 1) % n
            : (s_findHL.activeIdx - 1 + n) % n;
        NeHighlight_SetActive(hEdit, newIdx,
                              NE_HL_FG, NE_HL_BG, NE_HL_BG_INACTIVE, s_findHL);
    }

    Ne_UpdateFindCount();
}

static void Ne_DoReplace(HWND dlg, bool all)
{
    HWND hWhat = GetDlgItem(dlg, IDC_NE_DLG_FIND_WHAT);
    HWND hWith = GetDlgItem(dlg, IDC_NE_DLG_REPL_WITH);
    HWND hEdit = s_hwndFindEdit;
    if (!hWhat || !hWith || !hEdit) return;

    wchar_t what[512] = {}, with_[512] = {};
    GetWindowTextW(hWhat, what, 512);
    GetWindowTextW(hWith, with_, 512);
    if (!what[0]) return;
    std::wstring needle(what), replacement(with_);

    bool matchCase = (SendMessageW(GetDlgItem(dlg, IDC_NE_DLG_MATCHCASE), BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool wholeWord = (SendMessageW(GetDlgItem(dlg, IDC_NE_DLG_WHOLEWORD), BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool useRegex  = (SendMessageW(GetDlgItem(dlg, IDC_NE_DLG_REGEX),     BM_GETCHECK, 0, 0) == BST_CHECKED);

    // Helper: build wregex (shows error and returns false on bad pattern).
    auto buildRegex = [&](std::wregex& re) -> bool {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!matchCase) flags |= std::regex_constants::icase;
            re = std::wregex(needle, flags);
            return true;
        } catch (...) {
            MessageBoxW(dlg, Ls(L"MSG_REGEX_ERR"), Ls(L"DLG_FIND"), MB_OK | MB_ICONERROR);
            return false;
        }
    };

    if (all) {
        std::wstring text = Ne_GetEditText(hEdit);
        int count = 0;
        if (useRegex) {
            std::wregex re;
            if (!buildRegex(re)) return;
            struct MR { int s, e; std::wstring repl; };
            std::vector<MR> mrs;
            for (auto it = std::wsregex_iterator(text.begin(), text.end(), re);
                 it != std::wsregex_iterator(); ++it) {
                int s = (int)it->position();
                mrs.push_back({ s, s + (int)it->length(), it->format(replacement) });
            }
            for (int i = (int)mrs.size() - 1; i >= 0; i--) {
                CHARRANGE sel = { (LONG)mrs[i].s, (LONG)mrs[i].e };
                SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&sel);
                SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)mrs[i].repl.c_str());
                count++;
            }
        } else {
            int searchFrom = 0;
            std::vector<int> positions;
            while (true) {
                int pos = Ne_FindInText(text, needle, searchFrom, true, matchCase, wholeWord);
                if (pos < 0) break;
                positions.push_back(pos);
                searchFrom = pos + (int)needle.size();
            }
            for (int i = (int)positions.size() - 1; i >= 0; i--) {
                CHARRANGE sel = { (LONG)positions[i], (LONG)(positions[i] + (int)needle.size()) };
                SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&sel);
                SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)replacement.c_str());
                count++;
            }
        }
        NeHighlight_Clear(hEdit, s_findHL);
        s_findCachedNeedle.clear();
        Ne_UpdateFindCount();
        if (count == 0)
            MessageBoxW(dlg, Ls(L"MSG_FIND_NOT_FOUND"), Ls(L"DLG_FIND"), MB_OK | MB_ICONINFORMATION);
    } else {
        // Replace the current active highlighted match, then find next.
        if (useRegex && !s_findMatches.empty() && s_findHL.activeIdx >= 0) {
            const auto& mr = s_findMatches[s_findHL.activeIdx];
            CHARRANGE sel = { (LONG)mr.start, (LONG)mr.end };
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&sel);
            int len = mr.end - mr.start;
            std::wstring matchText(len, L'\0');
            SendMessageW(hEdit, EM_GETSELTEXT, 0, (LPARAM)matchText.data());
            std::wregex re;
            if (buildRegex(re)) {
                std::wsmatch m;
                std::wstring expanded = replacement;
                if (std::regex_search(matchText, m, re)) expanded = m.format(replacement);
                SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)expanded.c_str());
            }
        } else if (!useRegex) {
            CHARRANGE cr = {};
            SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            if (cr.cpMin != cr.cpMax) {
                int selLen = cr.cpMax - cr.cpMin;
                std::wstring selText(selLen + 1, L'\0');
                SendMessageW(hEdit, EM_GETSELTEXT, 0, (LPARAM)selText.data());
                selText.resize(selLen);
                bool matches = matchCase ? (selText == needle)
                                         : (_wcsicmp(selText.c_str(), needle.c_str()) == 0);
                if (matches)
                    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)replacement.c_str());
            }
        }
        NeHighlight_Clear(hEdit, s_findHL);
        s_findCachedNeedle.clear();
        Ne_UpdateFindCount();
        Ne_DoFindNext(dlg, true);
    }
}

static NeDialogData s_findBtnDD;

static LRESULT CALLBACK Ne_FindDlgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDCANCEL)                  { DestroyWindow(h); return 0; }
        if (id == IDOK)                      { Ne_DoFindNext(h, true); return 0; }  // Enter key
        if (id == IDC_NE_DLG_FIND_NEXT)      { Ne_DoFindNext(h, true);  return 0; }
        if (id == IDC_NE_DLG_REPLACE)        { Ne_DoReplace(h, false);  return 0; }
        if (id == IDC_NE_DLG_REPLACE_ALL)    { Ne_DoReplace(h, true);   return 0; }
        // Regex checkbox toggled — grey out MatchCase and WholeWord.
        if (id == IDC_NE_DLG_REGEX && HIWORD(w) == BN_CLICKED) {
            bool on = (SendMessageW(GetDlgItem(h, IDC_NE_DLG_REGEX), BM_GETCHECK, 0, 0) == BST_CHECKED);
            EnableWindow(GetDlgItem(h, IDC_NE_DLG_MATCHCASE), !on);
            EnableWindow(GetDlgItem(h, IDC_NE_DLG_WHOLEWORD), !on);
            InvalidateRect(GetDlgItem(h, IDC_NE_DLG_MATCHCASE), NULL, TRUE);
            InvalidateRect(GetDlgItem(h, IDC_NE_DLG_WHOLEWORD), NULL, TRUE);
            s_findCachedNeedle.clear();  // force rescan with new mode
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { DestroyWindow(h); return 0; }
        break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)l;
        if (DrawCustomCheckbox(dis)) return TRUE;
        if (dis->CtlType == ODT_BUTTON) {
            Ne_DrawDialogButton(dis, &s_findBtnDD);
            return TRUE;
        }
        break;
    }
    case WM_SETTINGCHANGE:
        OnCheckboxSettingChange(h);
        break;
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)w, RGB(20, 20, 20));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY: {
        NeHighlight_Clear(s_hwndFindEdit, s_findHL);
        s_findMatches.clear();
        s_findCachedNeedle.clear();
        HFONT hf = (HFONT)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (hf) DeleteObject(hf);
        for (int i = 0; i < s_findBtnDD.buttonCount; i++) {
            if (s_findBtnDD.buttons[i].hIconOverride) {
                DestroyIcon(s_findBtnDD.buttons[i].hIconOverride);
                s_findBtnDD.buttons[i].hIconOverride = NULL;
            }
        }
        s_hwndFind = NULL;
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static void Ne_ShowFindDialog(HWND parent, HWND hEdit)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    s_hwndFindEdit = hEdit;

    if (s_hwndFind && IsWindow(s_hwndFind)) {
        SetForegroundWindow(s_hwndFind);
        return;
    }

    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = Ne_FindDlgProc;
        wc.hInstance     = hi;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"NsbFindClass";
        RegisterClassW(&wc);
        s_registered = true;
    }

    // Build button specs with measured widths and shell32 icon overrides.
    s_findBtnDD = {};
    s_findBtnDD.buttonCount = 4;
    HICON hIconCheck = NULL, hIconClose = NULL;
    ExtractIconExW(L"shell32.dll", 294, NULL, &hIconCheck, 1);
    ExtractIconExW(L"shell32.dll", 131, NULL, &hIconClose,  1);
    s_findBtnDD.buttons[0] = { IDC_NE_DLG_FIND_NEXT,  Ls(L"BTN_FIND_NEXT"),   NeBtnTone::Blue,  IDI_INFORMATION, Ne_MeasureButtonWidth(Ls(L"BTN_FIND_NEXT")),   NULL       };
    s_findBtnDD.buttons[1] = { IDC_NE_DLG_REPLACE,    Ls(L"BTN_REPLACE"),     NeBtnTone::Green, IDI_INFORMATION, Ne_MeasureButtonWidth(Ls(L"BTN_REPLACE")),     hIconCheck };
    s_findBtnDD.buttons[2] = { IDC_NE_DLG_REPLACE_ALL,Ls(L"BTN_REPLACE_ALL"), NeBtnTone::Green, IDI_INFORMATION, Ne_MeasureButtonWidth(Ls(L"BTN_REPLACE_ALL")), hIconCheck };
    s_findBtnDD.buttons[3] = { IDCANCEL,               Ls(L"BTN_CLOSE"),       NeBtnTone::Red,   IDI_ERROR,       Ne_MeasureButtonWidth(Ls(L"BTN_CLOSE")),       hIconClose };

    const int P = S(10), LH = S(24), EB = S(22), CB = S(34);
    int totalBtnW = 0;
    for (int i = 0; i < 4; i++) totalBtnW += s_findBtnDD.buttons[i].width;
    totalBtnW += 3 * S(6);

    int clientW = std::max(S(450), totalBtnW + 2 * P);
    int clientH = P + (EB + P) + (EB + P) + LH + P + CB + P;
    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE,
                       WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    RECT pr = {}; if (parent) GetWindowRect(parent, &pr);
    int x = (pr.left + pr.right) / 2 - W / 2, y = (pr.top + pr.bottom) / 2 - H / 2;
    if (y < 30) y = 30;

    s_hwndFind = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"NsbFindClass", Ls(L"DLG_FIND"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!s_hwndFind) return;

    HFONT hf = Ne_MakeDlgFont(s_hwndFind);
    SetWindowLongPtrW(s_hwndFind, GWLP_USERDATA, (LONG_PTR)hf);

    RECT rc; GetClientRect(s_hwndFind, &rc);
    int y0 = P;

    auto mkLbl = [&](const wchar_t* t, int yy) {
        HWND ctrl = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
            P, yy + S(4), S(115), LH, s_hwndFind, NULL, hi, NULL);
        if (hf) SendMessageW(ctrl, WM_SETFONT, (WPARAM)hf, TRUE);
    };
    auto mkEdit = [&](int id, int yy) -> HWND {
        HWND ctrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            P + S(115), yy, rc.right - P * 2 - S(115), EB,
            s_hwndFind, (HMENU)(UINT_PTR)id, hi, NULL);
        if (hf) SendMessageW(ctrl, WM_SETFONT, (WPARAM)hf, TRUE);
        return ctrl;
    };
    auto mkCustomCheck = [&](int id, const wchar_t* t, int xx, int yy) -> HWND {
        HWND ctrl = CreateCustomCheckbox(s_hwndFind, id, t, false,
            xx, yy, S(130), LH, hi);
        if (ctrl && hf) SendMessageW(ctrl, WM_SETFONT, (WPARAM)hf, TRUE);
        return ctrl;
    };

    mkLbl(Ls(L"DLG_FIND_WHAT"), y0);
    // Find edit is narrower — leave room for the "3 / 14" counter on the right.
    {
        const int COUNT_W = S(72);
        int editW = rc.right - P * 2 - S(115) - COUNT_W - S(6);
        HWND hFW = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            P + S(115), y0, editW, EB,
            s_hwndFind, (HMENU)(UINT_PTR)IDC_NE_DLG_FIND_WHAT, hi, NULL);
        if (hf) SendMessageW(hFW, WM_SETFONT, (WPARAM)hf, TRUE);
        HWND hCount = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            rc.right - P - COUNT_W, y0 + S(4), COUNT_W, EB - S(4),
            s_hwndFind, (HMENU)(UINT_PTR)IDC_NE_DLG_FIND_COUNT, hi, NULL);
        if (hf) SendMessageW(hCount, WM_SETFONT, (WPARAM)hf, TRUE);
        SetFocus(hFW);
    }
    y0 += EB + P;

    mkLbl(Ls(L"DLG_REPL_WITH"), y0);
    mkEdit(IDC_NE_DLG_REPL_WITH, y0); y0 += EB + P;

    // Single row: MatchCase | WholeWord | Regex  (custom theme-aware checkboxes)
    mkCustomCheck(IDC_NE_DLG_MATCHCASE, Ls(L"CHK_MATCHCASE"), P,          y0);
    mkCustomCheck(IDC_NE_DLG_WHOLEWORD, Ls(L"CHK_WHOLEWORD"), P + S(140), y0);
    mkCustomCheck(IDC_NE_DLG_REGEX,     Ls(L"CHK_REGEX"),     P + S(280), y0);
    y0 += LH + P;

    // Owner-draw buttons, centred horizontally.
    int bx = (rc.right - totalBtnW) / 2;
    for (int i = 0; i < 4; i++) {
        auto& b = s_findBtnDD.buttons[i];
        DWORD sty = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
        if (b.id == IDC_NE_DLG_FIND_NEXT) sty |= BS_DEFPUSHBUTTON;
        HWND hBtn = CreateWindowExW(0, L"BUTTON", b.text.c_str(), sty,
            bx, y0, b.width, CB, s_hwndFind, (HMENU)(UINT_PTR)b.id, hi, NULL);
        if (hBtn) {
            WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
            SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
        }
        bx += b.width + S(6);
    }
}

// ── Insert Link dialog ────────────────────────────────────────────────────────
static struct { int result; wchar_t url[2048]; wchar_t text[2048]; } s_linkDD;
static NeDialogData s_linkBtnDD;

static LRESULT CALLBACK Ne_LinkDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_DRAWITEM:
        if (((const DRAWITEMSTRUCT*)lParam)->CtlType == ODT_BUTTON) {
            Ne_DrawDialogButton((const DRAWITEMSTRUCT*)lParam, &s_linkBtnDD);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            int id = LOWORD(wParam);
            if (id == IDOK) {
                wchar_t urlBuf[2048] = {};
                GetWindowTextW(GetDlgItem(hwnd, IDC_NE_DLG_LINK_URL), urlBuf, 2048);

                // Full regex URL validation.
                // label    = [a-zA-Z0-9][a-zA-Z0-9\-]*  (one char min, hyphens ok mid)
                // host     = (label.)+TLD
                // TLD      = [a-zA-Z]{2,4}  (user spec: 2-4 alpha only)
                // port     = :\d{1,5}
                // path     = /[^\s]*
                static const std::wregex s_urlRe(
                    // ── http / https / ftp / ftps ──────────────────────────────────
                    L"(https?|ftps?)://"
                    L"([a-zA-Z0-9][a-zA-Z0-9\\-]*\\.)+"
                    L"[a-zA-Z]{2,4}"
                    L"(:\\d{1,5})?"
                    L"(/[^\\s]*)?"
                    L"|"
                    // ── mailto ─────────────────────────────────────────────────────
                    L"mailto:"
                    L"[a-zA-Z0-9._%+\\-]+"
                    L"@"
                    L"([a-zA-Z0-9][a-zA-Z0-9\\-]*\\.)+"
                    L"[a-zA-Z]{2,4}"
                    L"|"
                    // ── file ───────────────────────────────────────────────────────
                    L"file://[^\\s]+"
                    L"|"
                    // ── bare www. (implied http) ───────────────────────────────────
                    L"www\\."
                    L"([a-zA-Z0-9][a-zA-Z0-9\\-]*\\.)+"
                    L"[a-zA-Z]{2,4}"
                    L"(:\\d{1,5})?"
                    L"(/[^\\s]*)?" ,
                    std::wregex::icase
                );

                bool valid = urlBuf[0] != L'\0' &&
                             std::regex_match(std::wstring(urlBuf), s_urlRe);

                if (!valid) {
                    HICON hIconWarn = LoadIconW(NULL, IDI_WARNING);
                    HICON hIconOk = NULL;
                    ExtractIconExW(L"shell32.dll", 294, NULL, &hIconOk, 1);
                    NeDialogButtonSpec okBtn = { IDOK, Ls(L"BTN_OK"),
                                                NeBtnTone::Blue, IDI_INFORMATION, 0, hIconOk };
                    Ne_ShowChoiceDialog(hwnd, Ls(L"DLG_LINK"),
                                        Ls(L"MSG_LINK_BAD_URL"), &okBtn, 1, IDOK, hIconWarn);
                    if (hIconOk) DestroyIcon(hIconOk);
                    SetFocus(GetDlgItem(hwnd, IDC_NE_DLG_LINK_URL));
                    return 0;
                }
                s_linkDD.result = IDOK;
                wcscpy_s(s_linkDD.url, urlBuf);
                GetWindowTextW(GetDlgItem(hwnd, IDC_NE_DLG_LINK_TEXT), s_linkDD.text, 2048);
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == IDCANCEL) {
                s_linkDD.result = IDCANCEL;
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wParam, RGB(20, 20, 20));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        s_linkDD.result = IDCANCEL;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ne_ShowLinkDialog(HWND parent, HWND hEdit)
{
    HINSTANCE hi = GetModuleHandleW(NULL);

    // Pre-fill URL if selection looks like a URL.
    std::wstring selUrl, selText;
    {
        CHARRANGE cr = {};
        SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
        int len = cr.cpMax - cr.cpMin;
        if (len > 0 && len < 2048) {
            std::wstring buf(len + 1, L'\0');
            SendMessageW(hEdit, EM_GETSELTEXT, 0, (LPARAM)buf.data());
            buf.resize(len);
            if (buf.find(L"http") == 0 || buf.find(L"www.") == 0)
                selUrl = buf;
            else
                selText = buf;
        }
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = Ne_LinkDlgProc;
        wc.hInstance     = hi;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"NsbLinkClass";
        RegisterClassW(&wc);
        registered = true;
    }

    const int P = S(10), EB = S(22), CB = S(34);
    int clientW = S(380);
    int clientH = P + (EB + P) + (EB + P * 2) + CB + P;
    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE,
                       WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    RECT pr = {}; if (parent) GetWindowRect(parent, &pr);
    int x = (pr.left + pr.right) / 2 - W / 2, y = (pr.top + pr.bottom) / 2 - H / 2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"NsbLinkClass", Ls(L"DLG_LINK"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    HFONT hf = Ne_MakeDlgFont(dlg);
    RECT rc; GetClientRect(dlg, &rc);
    const int LH = S(22);
    int y0 = P;

    auto mkLbl = [&](const wchar_t* t, int yy) {
        HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
            P, yy + S(3), S(110), LH, dlg, NULL, hi, NULL);
        if (hf) SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
    };
    auto mkEditF = [&](int id, const wchar_t* init, int yy) -> HWND {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", init,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            P + S(115), yy, rc.right - P * 2 - S(115), EB,
            dlg, (HMENU)(UINT_PTR)id, hi, NULL);
        if (hf) SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
        return h;
    };

    mkLbl(Ls(L"DLG_LINK_URL"),  y0);
    HWND hUrl = mkEditF(IDC_NE_DLG_LINK_URL,  selUrl.c_str(),  y0); y0 += EB + P;
    mkLbl(Ls(L"DLG_LINK_TEXT"), y0);
    mkEditF(IDC_NE_DLG_LINK_TEXT, selText.c_str(), y0); y0 += EB + P*2;

    // Owner-draw buttons (same pattern as HR props / table props dialogs)
    s_linkBtnDD = {};
    s_linkBtnDD.buttonCount = 2;
    s_linkBtnDD.buttons[0] = { IDOK,     Ls(L"BTN_SAVE"),   NeBtnTone::Blue, IDI_INFORMATION, Ne_MeasureButtonWidth(Ls(L"BTN_SAVE"))   };
    s_linkBtnDD.buttons[1] = { IDCANCEL, Ls(L"BTN_CANCEL"), NeBtnTone::Red,  IDI_ERROR,       Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL")) };
    {
        int totalBtnW = s_linkBtnDD.buttons[0].width + S(6) + s_linkBtnDD.buttons[1].width;
        int bx2 = (rc.right - totalBtnW) / 2;
        for (int i = 0; i < s_linkBtnDD.buttonCount; i++) {
            auto& b = s_linkBtnDD.buttons[i];
            DWORD sty = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
            if (b.id == IDOK) sty |= BS_DEFPUSHBUTTON;
            HWND hBtn = CreateWindowExW(0, L"BUTTON", b.text.c_str(), sty,
                bx2, y0, b.width, CB, dlg, (HMENU)(UINT_PTR)b.id, hi, NULL);
            if (hBtn) {
                WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
                SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
            }
            bx2 += b.width + S(6);
        }
    }

    if (parent) EnableWindow(parent, FALSE);
    SetFocus(hUrl);

    s_linkDD.result = IDCANCEL;
    MSG m;
    while (IsWindow(dlg) && GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }

    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }

    if (s_linkDD.result == IDOK && s_linkDD.url[0]) {
        const wchar_t* url  = s_linkDD.url;
        const wchar_t* disp = s_linkDD.text[0] ? s_linkDD.text : url;
        // Build RTF hyperlink: {\field{\*\fldinst{HYPERLINK "url"}}{\fldrslt display}}
        std::wstring rtfW = L"{\\rtf1\\ansi{\\field{\\*\\fldinst{HYPERLINK \"";
        rtfW += url;
        rtfW += L"\"}}{\\fldrslt ";
        rtfW += disp;
        rtfW += L"}}";
        rtfW += L"}";
        // Convert to narrow for EM_STREAMIN.
        int nb = WideCharToMultiByte(CP_ACP, 0, rtfW.c_str(), -1, NULL, 0, NULL, NULL);
        std::string rtfA(nb, '\0');
        WideCharToMultiByte(CP_ACP, 0, rtfW.c_str(), -1, &rtfA[0], nb, NULL, NULL);
        NeStreamBuf rb = { &rtfA, 0 };
        EDITSTREAM es = { (DWORD_PTR)&rb, 0, Ne_ReadCb };
        SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
    }
    SetFocus(hEdit);
}

// ── Insert Table dialog ───────────────────────────────────────────────────────
// ── Table properties struct ───────────────────────────────────────────────────
struct NeTableProps {
    int rows      = 3;     // number of rows
    int cols      = 3;     // number of columns
    int cellW     = 1440;  // column width in twips (all cols equal)
    int borderW   = 15;    // border width in half-points (15 = ~0.5 pt)
    int gapH      = 108;   // \trgaph — half the inter-cell gap in twips
    int padTop    = 0;     // \trpaddt in twips
    int padBot    = 0;     // \trpaddb in twips
    int padLeft   = 108;   // \trpaddl in twips
    int padRight  = 108;   // \trpaddr in twips
    int rowH      = 0;     // \trrh; 0 = auto, >0 = minimum (twips)
    int tblAlign  = 0;     // 0=left,1=center,2=right  (\trql / \trqc / \trqr)
    int leftIndent = 0;    // \trleft in twips
};

// Returns true when the caret / selection is inside a table row.
// Does this by streaming out a single char around the caret as RTF and checking for \intbl.
static bool Ne_CaretInTable(HWND hEdit)
{
    CHARRANGE crOrig = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&crOrig);

    // Probe the character AT the caret position.
    // Use SFF_SELECTION so only that paragraph's properties are streamed,
    // not the entire document (which would always return \intbl if any table exists).
    int docLen = GetWindowTextLengthW(hEdit);
    int probeMin = crOrig.cpMin;
    int probeMax = probeMin + 1;
    if (probeMax > docLen) {
        probeMax = docLen;
        probeMin = docLen > 0 ? docLen - 1 : 0;
    }
    if (probeMin >= probeMax) return false; // empty document

    CHARRANGE crProbe = { probeMin, probeMax };
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crProbe);
    std::string rtf;
    EDITSTREAM es = { (DWORD_PTR)&rtf, 0, Ne_WriteCb };
    SendMessageW(hEdit, EM_STREAMOUT, SF_RTF | SFF_SELECTION, (LPARAM)&es);
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crOrig);
    return rtf.find("\\intbl") != std::string::npos;
}

// Parse the first \trowd block from an RTF string into NeTableProps.
static NeTableProps Ne_ParseTableProps(const std::string& rtf)
{
    NeTableProps p;
    // Find first \trowd
    size_t pos = rtf.find("\\trowd");
    if (pos == std::string::npos) return p;
    // Find matching \row
    size_t rowEnd = rtf.find("\\row", pos);
    if (rowEnd == std::string::npos) rowEnd = rtf.size();
    std::string block = rtf.substr(pos, rowEnd - pos);

    auto readInt = [&](const std::string& key, int& out) {
        size_t k = block.find(key);
        if (k == std::string::npos) return;
        k += key.size();
        bool neg = (k < block.size() && block[k] == '-');
        if (neg) k++;
        if (k >= block.size() || !isdigit((unsigned char)block[k])) return;
        int v = 0;
        while (k < block.size() && isdigit((unsigned char)block[k]))
            v = v * 10 + (block[k++] - '0');
        out = neg ? -v : v;
    };

    readInt("\\trgaph",   p.gapH);
    readInt("\\trleft",   p.leftIndent);
    readInt("\\trpaddt",  p.padTop);
    readInt("\\trpaddb",  p.padBot);
    readInt("\\trpaddl",  p.padLeft);
    readInt("\\trpaddr",  p.padRight);
    readInt("\\trrh",     p.rowH);
    if (block.find("\\trqc") != std::string::npos) p.tblAlign = 1;
    else if (block.find("\\trqr") != std::string::npos) p.tblAlign = 2;
    else p.tblAlign = 0;

    // Count \cellx entries → columns
    int cols = 0;
    int lastCellX = 0;
    size_t cx = 0;
    while ((cx = block.find("\\cellx", cx)) != std::string::npos) {
        cx += 6;
        if (cx < block.size() && isdigit((unsigned char)block[cx])) {
            int v = 0;
            size_t t = cx;
            while (t < block.size() && isdigit((unsigned char)block[t]))
                v = v * 10 + (block[t++] - '0');
            lastCellX = v;
        }
        cols++;
    }
    if (cols > 0) p.cols = cols;
    if (cols > 0 && lastCellX > 0) p.cellW = lastCellX / cols;

    // Extract border width from first \clbrdrX\brdrs\brdrwN
    size_t bw = block.find("\\brdrw");
    if (bw != std::string::npos) {
        bw += 6;
        int v = 0;
        while (bw < block.size() && isdigit((unsigned char)block[bw]))
            v = v * 10 + (block[bw++] - '0');
        p.borderW = v;
    }

    // Count rows by counting \row occurrences in the full document section
    int rows = 0;
    size_t rp = 0;
    while ((rp = rtf.find("\\row", rp)) != std::string::npos) {
        // Make sure it's \row followed by non-alphanumeric (not \rowwidth etc.)
        size_t after = rp + 4;
        char next = (after < rtf.size()) ? rtf[after] : 0;
        if (!isalpha((unsigned char)next)) rows++;
        rp += 4;
    }
    if (rows > 0) p.rows = rows;

    return p;
}

// Build RTF for a full table from NeTableProps.
static std::string Ne_BuildTableRtf(const NeTableProps& p)
{
    const char* align = (p.tblAlign == 1) ? "\\trqc" :
                        (p.tblAlign == 2) ? "\\trqr" : "";
    std::string rtf = "{\\rtf1\\ansi ";
    for (int r = 0; r < p.rows; r++) {
        char hdr[256];
        snprintf(hdr, sizeof(hdr),
            "\\trowd%s\\trgaph%d\\trleft%d",
            align, p.gapH, p.leftIndent);
        rtf += hdr;
        if (p.padTop || p.padBot || p.padLeft || p.padRight) {
            char pad[128];
            snprintf(pad, sizeof(pad),
                "\\trpaddt%d\\trpaddb%d\\trpaddl%d\\trpaddr%d"
                "\\trpaddft3\\trpaddfb3\\trpaddfl3\\trpaddfr3",
                p.padTop, p.padBot, p.padLeft, p.padRight);
            rtf += pad;
        }
        if (p.rowH != 0) {
            char rh[32]; snprintf(rh, sizeof(rh), "\\trrh%d", p.rowH);
            rtf += rh;
        }
        for (int c = 0; c < p.cols; c++) {
            char bdr[256];
            snprintf(bdr, sizeof(bdr),
                "\\clbrdrt\\brdrs\\brdrw%d"
                "\\clbrdrl\\brdrs\\brdrw%d"
                "\\clbrdrb\\brdrs\\brdrw%d"
                "\\clbrdrr\\brdrs\\brdrw%d",
                p.borderW, p.borderW, p.borderW, p.borderW);
            rtf += bdr;
            char cx[32];
            snprintf(cx, sizeof(cx), "\\cellx%d", p.cellW * (c + 1) + p.leftIndent);
            rtf += cx;
        }
        for (int c = 0; c < p.cols; c++)
            rtf += "\\intbl\\cell";
        rtf += "\\row\r\n";
    }
    rtf += "\\pard\\par}";
    return rtf;
}

// ── Control IDs used inside the table-properties dialog ──────────────────────
#define IDC_TBLP_ROWS      260
#define IDC_TBLP_COLS      261
#define IDC_TBLP_CELLW     262
#define IDC_TBLP_BORDERW   263
#define IDC_TBLP_PADTOP    264
#define IDC_TBLP_PADBOTTOM 265
#define IDC_TBLP_PADLEFT   266
#define IDC_TBLP_PADRIGHT  267
#define IDC_TBLP_ROWH      268
#define IDC_TBLP_ALIGN_L   270
#define IDC_TBLP_ALIGN_C   271
#define IDC_TBLP_ALIGN_R   272
#define IDC_TBLP_MODE_ALTER  273
#define IDC_TBLP_MODE_NESTED 274

static NeDialogData s_tblPropsDD;
static NeTableProps s_tblPropsResult;
static bool         s_tblPropsModeAlter; // true = alter existing table, false = insert nested

static LRESULT CALLBACK Ne_TblPropsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            int cmd = LOWORD(wParam);
            if (cmd == IDOK) {
                // Read all control values BEFORE DestroyWindow kills the children
                wchar_t buf[16];
                auto rd = [&](int id) -> int {
                    HWND h = GetDlgItem(hwnd, id);
                    if (!h) return 0;
                    GetWindowTextW(h, buf, 16);
                    return _wtoi(buf);
                };
                s_tblPropsResult = {};
                s_tblPropsResult.rows     = std::max(1, rd(IDC_TBLP_ROWS));
                s_tblPropsResult.cols     = std::max(1, rd(IDC_TBLP_COLS));
                int cellMm = std::max(5, rd(IDC_TBLP_CELLW));
                s_tblPropsResult.cellW    = (int)(cellMm * 56.7 + 0.5);
                s_tblPropsResult.borderW  = std::max(0, rd(IDC_TBLP_BORDERW));
                s_tblPropsResult.padTop   = std::max(0, rd(IDC_TBLP_PADTOP))    * 20;
                s_tblPropsResult.padBot   = std::max(0, rd(IDC_TBLP_PADBOTTOM)) * 20;
                s_tblPropsResult.padLeft  = std::max(0, rd(IDC_TBLP_PADLEFT))   * 20;
                s_tblPropsResult.padRight = std::max(0, rd(IDC_TBLP_PADRIGHT))  * 20;
                s_tblPropsResult.rowH     = std::max(0, rd(IDC_TBLP_ROWH))      * 20;
                s_tblPropsResult.tblAlign =
                    SendMessageW(GetDlgItem(hwnd, IDC_TBLP_ALIGN_C), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1
                  : SendMessageW(GetDlgItem(hwnd, IDC_TBLP_ALIGN_R), BM_GETCHECK, 0, 0) == BST_CHECKED ? 2 : 0;
                s_tblPropsResult.leftIndent = 0;
                s_tblPropsResult.gapH       = 108;
                // Mode (only relevant when inTable)
                s_tblPropsModeAlter =
                    GetDlgItem(hwnd, IDC_TBLP_MODE_ALTER) == NULL  // not in table → always insert
                    || SendMessageW(GetDlgItem(hwnd, IDC_TBLP_MODE_ALTER), BM_GETCHECK, 0, 0) == BST_CHECKED;
                s_tblPropsDD.result = IDOK;
                DestroyWindow(hwnd);
                return 0;
            }
            if (cmd == IDCANCEL) {
                s_tblPropsDD.result = IDCANCEL;
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_DRAWITEM:
        if (((const DRAWITEMSTRUCT*)lParam)->CtlType == ODT_BUTTON) {
            Ne_DrawDialogButton((const DRAWITEMSTRUCT*)lParam, &s_tblPropsDD);
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, RGB(20, 20, 20));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ne_ShowTablePropsDialog(HWND parent, HWND hEdit)
{
    HINSTANCE hi = GetModuleHandleW(NULL);

    // Detect context: are we inside a table already?
    bool inTable = Ne_CaretInTable(hEdit);
    NeTableProps props;
    if (inTable) {
        std::string full = Ne_StreamOut(hEdit, true);
        props = Ne_ParseTableProps(full);
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = Ne_TblPropsDlgProc;
        wc.hInstance     = hi;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"NsbTablePropsClass";
        RegisterClassW(&wc);
        registered = true;
    }

    // Pre-compute required client size so window fits all controls exactly.
    int neededClientH, neededClientW;
    {
        const int _P = S(10), _EB = S(22), _CB = S(34);
        int ny = _P;
        ny += _EB + S(6);   // rows
        ny += _EB + S(6);   // cols
        ny += _EB + S(10);  // cellW
        ny += _EB + S(6);   // borderW
        ny += _EB + S(4);   // padTop
        ny += _EB + S(4);   // padBot
        ny += _EB + S(4);   // padLeft
        ny += _EB + S(10);  // padRight
        ny += _EB + S(10);  // rowH
        ny += S(22) + S(2) + S(22) + S(8); // align label + radio row
        if (inTable) ny += S(22) + S(2) + S(22) + S(8); // mode label + radio row
        int bw0 = Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL"));
        int bw1 = Ne_MeasureButtonWidth(Ls(L"BTN_APPLY"));
        // Ensure wide enough for label+spin+unit columns and align group caption
        HDC _hdc = GetDC(NULL);
        HFONT _hf = Ne_CreateDialogFont(false);
        HFONT _hfOld = _hf ? (HFONT)SelectObject(_hdc, _hf) : NULL;
        SIZE _sz = {};
        const wchar_t* _alignTxt = Ls(L"TBLP_ALIGN");
        GetTextExtentPoint32W(_hdc, _alignTxt, (int)wcslen(_alignTxt), &_sz);
        if (_hfOld) SelectObject(_hdc, _hfOld);
        if (_hf) DeleteObject(_hf);
        ReleaseDC(NULL, _hdc);
        int minForAlign = _sz.cx + S(40); // caption + groupbox border padding
        neededClientW = std::max({ S(370), bw0 + bw1 + S(6) + 2 * _P, minForAlign });
        neededClientH = ny + _CB + _P;
    }
    RECT wr = { 0, 0, neededClientW, neededClientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    RECT pr = {}; if (parent) GetWindowRect(parent, &pr);
    int x = (pr.left + pr.right) / 2 - W / 2, y = (pr.top + pr.bottom) / 2 - H / 2;
    if (y < 30) y = 30;

    const wchar_t* title = inTable ? Ls(L"DLG_TABLE_PROPS") : Ls(L"DLG_TABLE_INSERT");
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"NsbTablePropsClass", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    HFONT hf = Ne_MakeDlgFont(dlg);
    RECT rc; GetClientRect(dlg, &rc);
    const int P = S(10), EB = S(22), LH = S(22), CB = S(34);
    const int LW = S(160), VX = S(170), VW = S(70);
    int y0 = P;

    // Helper: label + spin-edit pair
    auto mkLbl = [&](const wchar_t* t, int yy) {
        HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
            P, yy + S(3), LW, LH, dlg, NULL, hi, NULL);
        if (hf) SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
    };
    auto mkSpin = [&](int id, int yy, int initVal, int lo, int hi_) -> HWND {
        HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            VX, yy, VW, EB, dlg, (HMENU)(UINT_PTR)id, hi, NULL);
        if (hf) SendMessageW(hEd, WM_SETFONT, (WPARAM)hf, TRUE);
        wchar_t buf[16]; swprintf_s(buf, L"%d", initVal);
        SetWindowTextW(hEd, buf);
        HWND hSp = CreateWindowExW(0, UPDOWN_CLASSW, NULL,
            WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS,
            0, 0, 0, 0, dlg, NULL, hi, NULL);
        SendMessageW(hSp, UDM_SETBUDDY,   (WPARAM)hEd, 0);
        SendMessageW(hSp, UDM_SETRANGE32, lo, hi_);
        SendMessageW(hSp, UDM_SETPOS32,   0, initVal);
        return hEd;
    };
    // Unit label (e.g. "pt", "mm")
    auto mkUnit = [&](const wchar_t* t, int yy) {
        HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
            VX + VW + S(6), yy + S(3), S(60), LH, dlg, NULL, hi, NULL);
        if (hf) SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
    };

    // ── Mode (only shown when caret is inside a table) ───────────────────
    if (inTable) {
        const int RW2 = (rc.right - 2*P) / 2;
        struct { int id; const wchar_t* key; bool presel; } mrdos[] = {
            { IDC_TBLP_MODE_ALTER,  L"TBLP_MODE_ALTER",  true  },
            { IDC_TBLP_MODE_NESTED, L"TBLP_MODE_NESTED", false },
        };
        for (int i = 0; i < 2; i++) {
            DWORD grpFlag = (i == 0) ? WS_GROUP : 0;
            HWND h = CreateWindowExW(0, L"BUTTON", Ls(mrdos[i].key),
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | grpFlag,
                P + i * RW2, y0, RW2, LH, dlg,
                (HMENU)(UINT_PTR)mrdos[i].id, hi, NULL);
            if (hf) SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
            if (mrdos[i].presel) SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
        }
        y0 += LH + S(8);
    }

    // ── Structure ──────────────────────────────────────────────────────────
    mkLbl(Ls(L"TBLP_ROWS"),    y0);
    HWND hRows = mkSpin(IDC_TBLP_ROWS, y0, props.rows, 1, 100);
    y0 += EB + S(6);

    mkLbl(Ls(L"TBLP_COLS"),    y0);
    HWND hCols = mkSpin(IDC_TBLP_COLS, y0, props.cols, 1, 50);
    y0 += EB + S(6);

    // Column width: show in mm (1 mm ≈ 56.7 twips)
    int cellMm = (int)(props.cellW / 56.7 + 0.5);
    mkLbl(Ls(L"TBLP_CELLW"),   y0);
    HWND hCellW = mkSpin(IDC_TBLP_CELLW, y0, cellMm, 5, 250);
    mkUnit(L"mm", y0);
    y0 += EB + S(10);

    // ── Borders & padding ──────────────────────────────────────────────────
    // Border width: show in half-points (15 = 0.75 pt is default)
    mkLbl(Ls(L"TBLP_BORDERW"), y0);
    HWND hBorderW = mkSpin(IDC_TBLP_BORDERW, y0, props.borderW, 0, 200);
    mkUnit(Ls(L"TBLP_UNIT_HP"), y0);
    y0 += EB + S(6);

    // Cell padding (all sides in twips; show in pt, 1pt=20 twips)
    int padTopPt   = (props.padTop   + 10) / 20;
    int padBotPt   = (props.padBot   + 10) / 20;
    int padLeftPt  = (props.padLeft  + 10) / 20;
    int padRightPt = (props.padRight + 10) / 20;

    mkLbl(Ls(L"TBLP_PAD_TOP"),   y0);
    HWND hPadTop = mkSpin(IDC_TBLP_PADTOP, y0, padTopPt, 0, 100);
    mkUnit(L"pt", y0); y0 += EB + S(4);

    mkLbl(Ls(L"TBLP_PAD_BOTTOM"), y0);
    HWND hPadBot = mkSpin(IDC_TBLP_PADBOTTOM, y0, padBotPt, 0, 100);
    mkUnit(L"pt", y0); y0 += EB + S(4);

    mkLbl(Ls(L"TBLP_PAD_LEFT"),  y0);
    HWND hPadLeft = mkSpin(IDC_TBLP_PADLEFT, y0, padLeftPt, 0, 100);
    mkUnit(L"pt", y0); y0 += EB + S(4);

    mkLbl(Ls(L"TBLP_PAD_RIGHT"), y0);
    HWND hPadRight = mkSpin(IDC_TBLP_PADRIGHT, y0, padRightPt, 0, 100);
    mkUnit(L"pt", y0); y0 += EB + S(10);

    // ── Row height ────────────────────────────────────────────────────────
    int rowHPt = (int)(props.rowH / 20.0 + 0.5);
    mkLbl(Ls(L"TBLP_ROWH"),    y0);
    HWND hRowH = mkSpin(IDC_TBLP_ROWH, y0, rowHPt, 0, 1000);
    mkUnit(Ls(L"TBLP_ROWH_UNIT"), y0);
    y0 += EB + S(10);

    // ── Alignment radio group ─────────────────────────────────────────────
    {
        HFONT hfBold = Ne_MakeDlgFont(dlg, true);
        HWND hGrpLbl = CreateWindowExW(0, L"STATIC", Ls(L"TBLP_ALIGN"),
            WS_CHILD | WS_VISIBLE,
            P, y0, rc.right - 2*P, LH, dlg, NULL, hi, NULL);
        if (hfBold) SendMessageW(hGrpLbl, WM_SETFONT, (WPARAM)hfBold, TRUE);
        y0 += LH + S(2);

        const int RW = (rc.right - 2*P) / 3;
        struct { int id; const wchar_t* key; int align; } rdos[] = {
            { IDC_TBLP_ALIGN_L, L"TBLP_ALIGN_L", 0 },
            { IDC_TBLP_ALIGN_C, L"TBLP_ALIGN_C", 1 },
            { IDC_TBLP_ALIGN_R, L"TBLP_ALIGN_R", 2 },
        };
        for (int i = 0; i < 3; i++) {
            DWORD grpFlag = (i == 0) ? WS_GROUP : 0;
            HWND h = CreateWindowExW(0, L"BUTTON", Ls(rdos[i].key),
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | grpFlag,
                P + i * RW, y0, RW, LH, dlg,
                (HMENU)(UINT_PTR)rdos[i].id, hi, NULL);
            if (hf) SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
            if (rdos[i].align == props.tblAlign)
                SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
        }
        y0 += LH + S(8);
    }

    // ── Apply / Cancel (owner-draw, icon-tinted, i18n-measured) ─────────────
    s_tblPropsDD = {};
    s_tblPropsDD.buttonCount = 2;
    s_tblPropsDD.buttons[0] = { IDOK,     Ls(L"BTN_APPLY"),  NeBtnTone::Blue, IDI_INFORMATION, Ne_MeasureButtonWidth(Ls(L"BTN_APPLY"))  };
    s_tblPropsDD.buttons[1] = { IDCANCEL, Ls(L"BTN_CANCEL"), NeBtnTone::Red,  IDI_ERROR,       Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL")) };

    {
        int totalBtnW = 0;
        for (int i = 0; i < s_tblPropsDD.buttonCount; i++) {
            totalBtnW += s_tblPropsDD.buttons[i].width;
            if (i + 1 < s_tblPropsDD.buttonCount) totalBtnW += S(6);
        }
        int bx = (rc.right - totalBtnW) / 2;
        for (int i = 0; i < s_tblPropsDD.buttonCount; i++) {
            auto& b = s_tblPropsDD.buttons[i];
            DWORD sty = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
            if (b.id == IDOK) sty |= BS_DEFPUSHBUTTON;
            HWND hBtn = CreateWindowExW(0, L"BUTTON", b.text.c_str(), sty,
                bx, y0, b.width, CB, dlg, (HMENU)(UINT_PTR)b.id, hi, NULL);
            if (hBtn) {
                WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
                SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
            }
            bx += b.width + S(6);
        }
    }

    if (parent) EnableWindow(parent, FALSE);
    SetFocus(hRows);

    MSG m;
    s_tblPropsDD.result = IDCANCEL;
    while (IsWindow(dlg) && GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { DestroyWindow(dlg); break; }
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    bool ok = (s_tblPropsDD.result == IDOK);
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }

    if (ok) {
        std::string tableRtf = Ne_BuildTableRtf(s_tblPropsResult);
        NeStreamBuf sb = { &tableRtf, 0 };
        EDITSTREAM es  = { (DWORD_PTR)&sb, 0, Ne_ReadCb };
        if (inTable && s_tblPropsModeAlter) {
            // Replace existing table: extend selection to cover all \intbl paragraphs
            // around the caret, then stream in the replacement.
            CHARRANGE cr;
            SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            int lineCount  = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
            int caretLine  = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, (WPARAM)cr.cpMin, 0);
            int firstRow   = caretLine, lastRow = caretLine;
            SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
            for (int ln = caretLine - 1; ln >= 0; ln--) {
                int idx = (int)SendMessageW(hEdit, EM_LINEINDEX, ln, 0);
                CHARRANGE tr = { idx, idx + 1 };
                SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&tr);
                std::string lineRtf = Ne_StreamOut(hEdit, false);
                if (lineRtf.find("\\intbl") != std::string::npos) firstRow = ln;
                else break;
            }
            for (int ln = caretLine + 1; ln < lineCount; ln++) {
                int idx = (int)SendMessageW(hEdit, EM_LINEINDEX, ln, 0);
                CHARRANGE tr = { idx, idx + 1 };
                SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&tr);
                std::string lineRtf = Ne_StreamOut(hEdit, false);
                if (lineRtf.find("\\intbl") != std::string::npos) lastRow = ln;
                else break;
            }
            int selStart = (int)SendMessageW(hEdit, EM_LINEINDEX, firstRow, 0);
            int lastIdx  = (int)SendMessageW(hEdit, EM_LINEINDEX, lastRow, 0);
            int lastLen  = (int)SendMessageW(hEdit, EM_LINELENGTH, lastIdx, 0);
            CHARRANGE selRange = { selStart, lastIdx + lastLen };
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&selRange);
            SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
            SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
        } else {
            // Insert new table at caret (plain insert or nested)
            SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
        }
    }
    SetFocus(hEdit);
}

// ── RTF table insertion (shared by picker and dialog) ─────────────────────────
static void Ne_InsertTableRtf(HWND hEdit, int rows, int cols)
{
    const int cellW = 1440;
    std::string rtf = "{\\rtf1\\ansi ";
    for (int r = 0; r < rows; r++) {
        rtf += "\\trowd\\trgaph108\\trleft0";
        for (int c = 0; c < cols; c++) {
            rtf += "\\clbrdrt\\brdrs\\brdrw15"
                   "\\clbrdrl\\brdrs\\brdrw15"
                   "\\clbrdrb\\brdrs\\brdrw15"
                   "\\clbrdrr\\brdrs\\brdrw15";
            char cx[32]; snprintf(cx, sizeof(cx), "\\cellx%d", cellW * (c + 1));
            rtf += cx;
        }
        for (int c = 0; c < cols; c++)
            rtf += "\\intbl\\cell";
        rtf += "\\row\r\n";
    }
    rtf += "\\pard\\par}";
    NeStreamBuf sb = { &rtf, 0 };
    EDITSTREAM es  = { (DWORD_PTR)&sb, 0, Ne_ReadCb };
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
}

// ── Table size grid picker (Word-style hover popup) ───────────────────────────
static struct {
    HWND  hEdit;
    int   hoverC, hoverR;
    HFONT hFont;
} g_tblPick = {};

static LRESULT CALLBACK Ne_TablePickerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const int MAXC = 8, MAXR = 8;
    const int CW = S(24), CH = S(20), PAD = S(6), LBLH = S(22);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        // Background — tooltip colour
        HBRUSH hBg = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
        FillRect(hdc, &rc, hBg);
        DeleteObject(hBg);

        // Outer border (1 px black, like tooltip)
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN hOldP = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, rc.left,      rc.top,        NULL);
        LineTo  (hdc, rc.right - 1, rc.top);
        LineTo  (hdc, rc.right - 1, rc.bottom - 1);
        LineTo  (hdc, rc.left,      rc.bottom - 1);
        LineTo  (hdc, rc.left,      rc.top);
        SelectObject(hdc, hOldP);
        DeleteObject(hPen);

        // Grid cells
        for (int r = 0; r < MAXR; r++) {
            for (int c = 0; c < MAXC; c++) {
                int x1 = PAD + c * CW, y1 = PAD + r * CH;
                int x2 = x1 + CW,      y2 = y1 + CH;
                bool sel = (c < g_tblPick.hoverC && r < g_tblPick.hoverR);
                COLORREF fill   = sel ? RGB(180, 215, 255) : GetSysColor(COLOR_WINDOW);
                COLORREF border = sel ? RGB(0, 120, 215)   : RGB(160, 160, 170);
                HBRUSH hCb = CreateSolidBrush(fill);
                RECT cr = { x1 + 1, y1 + 1, x2, y2 };
                FillRect(hdc, &cr, hCb);
                DeleteObject(hCb);
                HPEN hCp = CreatePen(PS_SOLID, 1, border);
                HPEN hOC = (HPEN)SelectObject(hdc, hCp);
                MoveToEx(hdc, x1, y1, NULL);
                LineTo(hdc, x2, y1); LineTo(hdc, x2, y2);
                LineTo(hdc, x1, y2); LineTo(hdc, x1, y1);
                SelectObject(hdc, hOC); DeleteObject(hCp);
            }
        }

        // Label area
        SetBkMode(hdc, TRANSPARENT);
        if (g_tblPick.hFont) SelectObject(hdc, g_tblPick.hFont);
        wchar_t lbl[48];
        if (g_tblPick.hoverC > 0 && g_tblPick.hoverR > 0)
            swprintf_s(lbl, Ls(L"TABLE_PICKER_FMT"), g_tblPick.hoverR, g_tblPick.hoverC);
        else
            wcscpy_s(lbl, Ls(L"TABLE_PICKER_HINT"));
        SetTextColor(hdc, RGB(0, 0, 0));
        RECT lr = { PAD, PAD + MAXR * CH + S(3), rc.right - PAD, rc.bottom - S(2) };
        DrawTextW(hdc, lbl, -1, &lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        int nc = (mx < PAD || my < PAD) ? 0 : std::min((mx - PAD) / CW + 1, MAXC);
        int nr = (mx < PAD || my < PAD) ? 0 : std::min((my - PAD) / CH + 1, MAXR);
        if (my >= PAD + MAXR * CH) { nc = 0; nr = 0; }
        if (nc != g_tblPick.hoverC || nr != g_tblPick.hoverR) {
            g_tblPick.hoverC = nc;
            g_tblPick.hoverR = nr;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        int selC = (mx >= PAD) ? std::min((mx - PAD) / CW + 1, MAXC) : 0;
        int selR = (my >= PAD) ? std::min((my - PAD) / CH + 1, MAXR) : 0;
        bool inGrid = (selC >= 1 && selR >= 1 && my < PAD + MAXR * CH);
        HWND hEd = g_tblPick.hEdit;
        int r = selR, c = selC;
        DestroyWindow(hwnd); // triggers WM_DESTROY which frees the font
        if (inGrid) { Ne_InsertTableRtf(hEd, r, c); SetFocus(hEd); }
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_tblPick.hFont) { DeleteObject(g_tblPick.hFont); g_tblPick.hFont = NULL; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ne_ShowTablePicker(HWND hwndParent, HWND hEdit, HWND hBtn)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    const int MAXC = 8, MAXR = 8;
    const int CW = S(24), CH = S(20), PAD = S(6), LBLH = S(22);
    const int W = PAD * 2 + MAXC * CW;
    const int H = PAD * 2 + MAXR * CH + S(4) + LBLH;

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = Ne_TablePickerProc;
        wc.hInstance     = hi;
        wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = L"NsbTablePickerClass";
        RegisterClassW(&wc);
        registered = true;
    }

    // Position below the TABLE button; flip above if near screen bottom.
    RECT br = {}; GetWindowRect(hBtn, &br);
    int x = br.left, y = br.bottom + S(2);
    if (y + H > GetSystemMetrics(SM_CYSCREEN)) y = br.top - H - S(2);

    g_tblPick.hEdit  = hEdit;
    g_tblPick.hoverC = 0;
    g_tblPick.hoverR = 0;
    if (g_tblPick.hFont) { DeleteObject(g_tblPick.hFont); g_tblPick.hFont = NULL; }
    g_tblPick.hFont  = Ne_MakeDlgFont(hwndParent, true); // bold, matches tooltip style

    HWND hPick = CreateWindowExW(WS_EX_TOPMOST,
        L"NsbTablePickerClass", L"",
        WS_POPUP | WS_VISIBLE,
        x, y, W, H, hwndParent, NULL, hi, NULL);
    if (hPick) SetFocus(hPick);
}

// ── Table dialog (kept for programmatic/large-table use) ─────────────────────
static void Ne_ShowTableDialog(HWND parent, HWND hEdit)
{
    HINSTANCE hi = GetModuleHandleW(NULL);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NsbTableClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) RegisterClassW(&wc);

    const int W = S(300), H = S(155);
    RECT pr = {}; if (parent) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2-W/2, y = (pr.top+pr.bottom)/2-H/2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NsbTableClass", Ls(L"DLG_TABLE"),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    HFONT hf = Ne_MakeDlgFont(dlg);
    RECT rc; GetClientRect(dlg, &rc);
    const int P = S(10), EB = S(22), LH = S(24), CB = S(26);
    int y0 = P;

    auto mkLbl = [&](const wchar_t* t, int xx, int yy) {
        HWND h = CreateWindowExW(0,L"STATIC",t,WS_CHILD|WS_VISIBLE, xx,yy+S(3),S(100),LH,dlg,NULL,hi,NULL);
        if(hf) SendMessageW(h,WM_SETFONT,(WPARAM)hf,TRUE);
    };
    auto mkSpin = [&](int id, int xx, int yy, int initVal) -> HWND {
        HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_CENTER,
            xx, yy, S(50), EB, dlg, (HMENU)(UINT_PTR)id, hi, NULL);
        if(hf) SendMessageW(hEd,WM_SETFONT,(WPARAM)hf,TRUE);
        wchar_t buf[8]; swprintf_s(buf, L"%d", initVal);
        SetWindowTextW(hEd, buf);
        // Buddy spinner.
        HWND hSp = CreateWindowExW(0, UPDOWN_CLASSW, NULL,
            WS_CHILD|WS_VISIBLE|UDS_ALIGNRIGHT|UDS_SETBUDDYINT|UDS_ARROWKEYS,
            0,0,0,0, dlg, NULL, hi, NULL);
        SendMessageW(hSp, UDM_SETBUDDY,  (WPARAM)hEd, 0);
        SendMessageW(hSp, UDM_SETRANGE32, 1, 20);
        SendMessageW(hSp, UDM_SETPOS32,   0, initVal);
        return hEd;
    };

    mkLbl(Ls(L"DLG_TABLE_ROWS"), P, y0);
    HWND hRows = mkSpin(IDC_NE_DLG_TABLE_ROWS, P+S(105), y0, 3); y0 += EB + P;
    mkLbl(Ls(L"DLG_TABLE_COLS"), P, y0);
    HWND hCols = mkSpin(IDC_NE_DLG_TABLE_COLS, P+S(105), y0, 3); y0 += EB + P*2;

    int bw = S(80);
    int bx = rc.right - P - bw;
    auto mkBtnT = [&](int id, const wchar_t* t, int xx, bool def_) {
        DWORD sty = WS_CHILD|WS_VISIBLE|(def_?BS_DEFPUSHBUTTON:BS_PUSHBUTTON);
        HWND h = CreateWindowExW(0,L"BUTTON",t,sty, xx,y0,bw,CB, dlg,(HMENU)(UINT_PTR)id,hi,NULL);
        if(hf) SendMessageW(h,WM_SETFONT,(WPARAM)hf,TRUE);
    };
    mkBtnT(IDCANCEL, Ls(L"BTN_CANCEL"), bx, false); bx -= bw + S(6);
    mkBtnT(IDOK,     Ls(L"BTN_SAVE"),   bx, true);

    if (parent) EnableWindow(parent, FALSE);
    SetFocus(hRows);

    MSG m; bool ok = false;
    while (IsWindow(dlg) && GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { DestroyWindow(dlg); break; }
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (!IsWindow(dlg)) break;
        if (m.message == WM_COMMAND && LOWORD(m.wParam) == IDOK && m.hwnd == dlg) { ok = true; break; }
    }
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }

    if (ok && IsWindow(dlg)) {
        wchar_t rb[8]={}, cb[8]={};
        GetWindowTextW(hRows, rb, 8); GetWindowTextW(hCols, cb, 8);
        DestroyWindow(dlg);
        int rows = std::max(1, _wtoi(rb)), cols = std::max(1, _wtoi(cb));
        Ne_InsertTableRtf(hEdit, rows, cols);
    } else if (IsWindow(dlg)) {
        DestroyWindow(dlg);
    }
    SetFocus(hEdit);
}

// ── Horizontal-rule properties dialog ─────────────────────────────────────────
#define IDC_HRP_STYLE        280
#define IDC_HRP_THICKNESS    281
#define IDC_HRP_SPACE_ABOVE  282
#define IDC_HRP_SPACE_BELOW  283
#define IDC_HRP_COLOR        284   // "Choose Color…" button
#define IDC_HRP_SWATCH       285   // color preview static
#define IDC_HRP_WIDTH        286
#define IDC_HRP_ALIGN_L      287
#define IDC_HRP_ALIGN_C      288
#define IDC_HRP_ALIGN_R      289

struct NeHRuleProps {
    int      styleIdx;    // 0=single thin .. 5=hairline
    int      thickness;   // half-points (stored in wBorderWidth)
    int      spaceBefore; // twips
    int      spaceAfter;  // twips
    COLORREF color;       // 24-bit RGB
    int      widthPct;    // 10-100 (% of paragraph width)
    int      align;       // 0=left, 1=center, 2=right
};

static const wchar_t* s_hrStyleName[6] = {
    L"Single thin", L"Thick", L"Double", L"Dotted", L"Dashed", L"Hairline",
};

static NeDialogData s_hrPropsDD;
static NeHRuleProps s_hrPropsResult;
static HBRUSH       s_hrSwatchBrush  = NULL;
static COLORREF     s_hrCustomColors[16] = {};

static LRESULT CALLBACK Ne_HRulePropsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            int cmd = LOWORD(wParam);
            if (cmd == IDC_HRP_COLOR) {
                CHOOSECOLORW cc = {};
                cc.lStructSize  = sizeof(cc);
                cc.hwndOwner    = hwnd;
                cc.lpCustColors = s_hrCustomColors;
                cc.rgbResult    = (COLORREF)(DWORD_PTR)GetWindowLongPtrW(
                                      GetDlgItem(hwnd, IDC_HRP_SWATCH), GWLP_USERDATA);
                cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    HWND hSwatch = GetDlgItem(hwnd, IDC_HRP_SWATCH);
                    SetWindowLongPtrW(hSwatch, GWLP_USERDATA, (LONG_PTR)(DWORD_PTR)cc.rgbResult);
                    InvalidateRect(hSwatch, NULL, TRUE);
                }
                return 0;
            }
            if (cmd == IDOK) {
                wchar_t buf[16];
                auto rd = [&](int id) -> int {
                    HWND h = GetDlgItem(hwnd, id);
                    if (!h) return 0;
                    GetWindowTextW(h, buf, 16);
                    return _wtoi(buf);
                };
                s_hrPropsResult.styleIdx    = (int)SendMessageW(GetDlgItem(hwnd, IDC_HRP_STYLE), CB_GETCURSEL, 0, 0);
                s_hrPropsResult.thickness   = std::max(1, rd(IDC_HRP_THICKNESS));
                s_hrPropsResult.spaceBefore = std::max(0, rd(IDC_HRP_SPACE_ABOVE)) * 20;
                s_hrPropsResult.spaceAfter  = std::max(0, rd(IDC_HRP_SPACE_BELOW)) * 20;
                s_hrPropsResult.color       = (COLORREF)(DWORD_PTR)GetWindowLongPtrW(
                                                  GetDlgItem(hwnd, IDC_HRP_SWATCH), GWLP_USERDATA);
                s_hrPropsResult.widthPct    = std::max(10, std::min(100, rd(IDC_HRP_WIDTH)));
                s_hrPropsResult.align       =
                    (SendMessageW(GetDlgItem(hwnd, IDC_HRP_ALIGN_L), BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 :
                    (SendMessageW(GetDlgItem(hwnd, IDC_HRP_ALIGN_R), BM_GETCHECK, 0, 0) == BST_CHECKED) ? 2 : 1;
                if (s_hrPropsResult.styleIdx < 0) s_hrPropsResult.styleIdx = 0;
                s_hrPropsDD.result = IDOK;
                DestroyWindow(hwnd);
                return 0;
            }
            if (cmd == IDCANCEL) {
                s_hrPropsDD.result = IDCANCEL;
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_DRAWITEM:
        if (((const DRAWITEMSTRUCT*)lParam)->CtlType == ODT_BUTTON) {
            Ne_DrawDialogButton((const DRAWITEMSTRUCT*)lParam, &s_hrPropsDD);
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HWND hCtrl = (HWND)lParam;
        if (GetDlgCtrlID(hCtrl) == IDC_HRP_SWATCH) {
            COLORREF col = (COLORREF)(DWORD_PTR)GetWindowLongPtrW(hCtrl, GWLP_USERDATA);
            if (s_hrSwatchBrush) { DeleteObject(s_hrSwatchBrush); s_hrSwatchBrush = NULL; }
            s_hrSwatchBrush = CreateSolidBrush(col);
            SetBkColor((HDC)wParam, col);
            return (LRESULT)s_hrSwatchBrush;
        }
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wParam, RGB(20, 20, 20));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ne_ApplyHRuleProps(HWND hEdit, const NeHRuleProps& p)
{
    // wBorders (PARAFORMAT2):
    //   Bits  0-7:  which sides (0x08 = bottom border)
    //   Bits  8-11: style (1=¾pt solid, 2=1½pt, 3=2¼pt, 4=3pt, 5=4½pt, 6=6pt…)
    //   Bits 12-15: fixed-palette color index (ignored; color set via CHARFORMAT2W)
    // Compute left/right indents to achieve width% and alignment
    RECT fmtRc = {};
    SendMessageW(hEdit, EM_GETRECT, 0, (LPARAM)&fmtRc);
    int pixW = fmtRc.right - fmtRc.left;
    HDC hdc  = GetDC(hEdit);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hEdit, hdc);
    int twipW   = (dpiX > 0) ? MulDiv(pixW, 1440, dpiX) : 8640;
    int narrow  = twipW - MulDiv(twipW, p.widthPct, 100);
    LONG leftIn = (p.align == 0) ? 0L : (p.align == 2) ? (LONG)narrow : (LONG)(narrow / 2);
    LONG rightIn = (LONG)narrow - leftIn;

    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask        = PFM_BORDER | PFM_STYLE | PFM_SPACEBEFORE | PFM_SPACEAFTER
                     | PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_ALIGNMENT;
    pf.sStyle        = 42;   // HR marker — more reliably stored than wBorders
    pf.wBorders      = (WORD)(0x08 | ((p.styleIdx + 1) << 8));
    pf.wBorderWidth  = (WORD)(p.thickness * 20);  // points → twips
    pf.wBorderSpace  = 20;
    pf.dySpaceBefore = (SHORT)p.spaceBefore;
    pf.dySpaceAfter  = (SHORT)p.spaceAfter;
    pf.dxStartIndent = leftIn;
    pf.dxRightIndent = rightIn;
    pf.wAlignment    = (p.align == 0) ? PFA_LEFT : (p.align == 2) ? PFA_RIGHT : PFA_CENTER;
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    // Border is rendered in the paragraph's text color
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask      = CFM_COLOR;
    cf.dwEffects   = 0;
    cf.crTextColor = p.color;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static NeHRuleProps Ne_ReadHRuleProps(HWND hEdit)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_BORDER | PFM_SPACEBEFORE | PFM_SPACEAFTER
              | PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_ALIGNMENT;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    NeHRuleProps p = {};
    // style in bits 8-11
    int style = (pf.wBorders >> 8) & 0x0F;
    p.styleIdx    = (style > 0) ? std::min(style - 1, 5) : 0;
    p.thickness   = pf.wBorderWidth > 0 ? std::max(1, (int)(pf.wBorderWidth / 20)) : 1;
    p.spaceBefore = pf.dySpaceBefore > 0 ? (int)pf.dySpaceBefore : 60;
    p.spaceAfter  = pf.dySpaceAfter  > 0 ? (int)pf.dySpaceAfter  : 60;
    p.color       = (cf.dwEffects & CFE_AUTOCOLOR) ? RGB(128,128,128) : cf.crTextColor;
    // Reconstruct widthPct and align from indents
    RECT fmtRc = {};
    SendMessageW(hEdit, EM_GETRECT, 0, (LPARAM)&fmtRc);
    int pixW = fmtRc.right - fmtRc.left;
    HDC hdc  = GetDC(hEdit);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hEdit, hdc);
    int twipW  = (dpiX > 0) ? MulDiv(pixW, 1440, dpiX) : 8640;
    int narrow = (int)pf.dxStartIndent + (int)pf.dxRightIndent;
    p.widthPct = (twipW > 0) ? std::max(10, 100 - MulDiv(narrow, 100, twipW)) : 100;
    if      (pf.wAlignment == PFA_RIGHT)  p.align = 2;
    else if (pf.wAlignment == PFA_CENTER) p.align = 1;
    else                                  p.align = 0;
    return p;
}

static bool Ne_ShowHRulePropsDialog(HWND parent, HWND hEdit, bool insertMode, NeHRuleProps* inout)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = Ne_HRulePropsDlgProc;
        wc.hInstance     = hi;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"NsbHRulePropsClass";
        RegisterClassW(&wc);
        registered = true;
    }

    const int P = S(10), EB = S(22), LH = S(22), CB = S(34);
    const int LW = S(160), VX = S(170), VW = S(120);
    int rowH    = LH + S(4) + EB + S(8);
    int clientH = P;
    clientH += rowH;                     // style
    clientH += rowH;                     // thickness
    clientH += rowH;                     // space above
    clientH += rowH;                     // space below
    clientH += rowH;                     // width
    clientH += rowH;                     // alignment
    clientH += LH + S(4) + EB + S(12);  // color
    clientH += CB + P;                   // buttons
    int clientW = std::max(S(390), VX + 3 * S(70) + S(20));

    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    RECT pr = {}; if (parent) GetWindowRect(parent, &pr);
    int x = (pr.left + pr.right) / 2 - W / 2, y = (pr.top + pr.bottom) / 2 - H / 2;
    if (y < 30) y = 30;

    const wchar_t* title = insertMode ? L"Insert Horizontal Rule" : L"Horizontal Rule Properties";
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"NsbHRulePropsClass", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return false;

    HFONT hf = Ne_MakeDlgFont(dlg);
    RECT rc; GetClientRect(dlg, &rc);
    int y0 = P;

    auto mkLbl = [&](const wchar_t* t, int yy) {
        HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
            P, yy + S(3), LW, LH, dlg, NULL, hi, NULL);
        if (hf) SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
    };
    auto mkSpin = [&](int id, int yy, int initVal, int lo, int hi_) -> HWND {
        HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            VX, yy, S(70), EB, dlg, (HMENU)(UINT_PTR)id, hi, NULL);
        if (hf) SendMessageW(hEd, WM_SETFONT, (WPARAM)hf, TRUE);
        wchar_t buf[16]; swprintf_s(buf, L"%d", initVal);
        SetWindowTextW(hEd, buf);
        HWND hSp = CreateWindowExW(0, UPDOWN_CLASSW, NULL,
            WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS,
            0, 0, 0, 0, dlg, NULL, hi, NULL);
        SendMessageW(hSp, UDM_SETBUDDY,   (WPARAM)hEd, 0);
        SendMessageW(hSp, UDM_SETRANGE32, lo, hi_);
        SendMessageW(hSp, UDM_SETPOS32,   0, initVal);
        return hEd;
    };

    // Style
    mkLbl(L"Style:", y0); y0 += LH + S(4);
    HWND hStyleCtrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        VX, y0, VW, EB * 8, dlg, (HMENU)(UINT_PTR)IDC_HRP_STYLE, hi, NULL);
    if (hf) SendMessageW(hStyleCtrl, WM_SETFONT, (WPARAM)hf, TRUE);
    for (int i = 0; i < 6; i++)
        SendMessageW(hStyleCtrl, CB_ADDSTRING, 0, (LPARAM)s_hrStyleName[i]);
    SendMessageW(hStyleCtrl, CB_SETCURSEL, inout->styleIdx, 0);
    y0 += EB + S(8);

    // Thickness
    mkLbl(L"Thickness (pt):", y0); y0 += LH + S(4);
    HWND hThickCtrl = mkSpin(IDC_HRP_THICKNESS, y0, inout->thickness, 1, 20);
    (void)hThickCtrl;
    y0 += EB + S(8);

    // Space above
    mkLbl(L"Space above (pt):", y0); y0 += LH + S(4);
    HWND hAboveCtrl = mkSpin(IDC_HRP_SPACE_ABOVE, y0, inout->spaceBefore / 20, 0, 100);
    (void)hAboveCtrl;
    y0 += EB + S(8);

    // Space below
    mkLbl(L"Space below (pt):", y0); y0 += LH + S(4);
    HWND hBelowCtrl = mkSpin(IDC_HRP_SPACE_BELOW, y0, inout->spaceAfter / 20, 0, 100);
    (void)hBelowCtrl;
    y0 += EB + S(8);

    // Width
    mkLbl(L"Width (%):", y0); y0 += LH + S(4);
    HWND hWidthCtrl = mkSpin(IDC_HRP_WIDTH, y0, inout->widthPct, 10, 100);
    (void)hWidthCtrl;
    y0 += EB + S(8);

    // Alignment
    mkLbl(L"Alignment:", y0); y0 += LH + S(4);
    {
        const wchar_t* aTxt[3] = { L"Left", L"Center", L"Right" };
        int aIds[3] = { IDC_HRP_ALIGN_L, IDC_HRP_ALIGN_C, IDC_HRP_ALIGN_R };
        int aW = S(70);
        for (int i = 0; i < 3; i++) {
            DWORD sty = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
            if (i == 0) sty |= WS_GROUP;
            HWND hr = CreateWindowExW(0, L"BUTTON", aTxt[i], sty,
                VX + i * aW, y0, aW, EB, dlg, (HMENU)(UINT_PTR)aIds[i], hi, NULL);
            if (hf) SendMessageW(hr, WM_SETFONT, (WPARAM)hf, TRUE);
            if (i == inout->align) SendMessageW(hr, BM_SETCHECK, BST_CHECKED, 0);
        }
    }
    y0 += EB + S(8);

    // Color
    mkLbl(L"Color:", y0); y0 += LH + S(4);
    // Swatch — painted via WM_CTLCOLORSTATIC
    HWND hSwatch = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE,
        VX, y0, S(36), EB, dlg, (HMENU)(UINT_PTR)IDC_HRP_SWATCH, hi, NULL);
    SetWindowLongPtrW(hSwatch, GWLP_USERDATA, (LONG_PTR)(DWORD_PTR)inout->color);
    // "Choose…" button opens ChooseColorW
    HWND hChooseBtn = CreateWindowExW(0, L"BUTTON", L"Choose\u2026",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        VX + S(44), y0, S(90), EB, dlg, (HMENU)(UINT_PTR)IDC_HRP_COLOR, hi, NULL);
    if (hf) SendMessageW(hChooseBtn, WM_SETFONT, (WPARAM)hf, TRUE);
    y0 += EB + S(12);

    // Apply / Cancel buttons
    s_hrPropsDD = {};
    s_hrPropsDD.buttonCount = 2;
    s_hrPropsDD.buttons[0] = { IDOK,     Ls(L"BTN_APPLY"),  NeBtnTone::Blue, IDI_INFORMATION, Ne_MeasureButtonWidth(Ls(L"BTN_APPLY"))  };
    s_hrPropsDD.buttons[1] = { IDCANCEL, Ls(L"BTN_CANCEL"), NeBtnTone::Red,  IDI_ERROR,       Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL")) };
    {
        int totalBtnW = 0;
        for (int i = 0; i < s_hrPropsDD.buttonCount; i++) {
            totalBtnW += s_hrPropsDD.buttons[i].width;
            if (i + 1 < s_hrPropsDD.buttonCount) totalBtnW += S(6);
        }
        int bx = (rc.right - totalBtnW) / 2;
        for (int i = 0; i < s_hrPropsDD.buttonCount; i++) {
            auto& b = s_hrPropsDD.buttons[i];
            DWORD sty = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
            if (b.id == IDOK) sty |= BS_DEFPUSHBUTTON;
            HWND hBtn = CreateWindowExW(0, L"BUTTON", b.text.c_str(), sty,
                bx, y0, b.width, CB, dlg, (HMENU)(UINT_PTR)b.id, hi, NULL);
            if (hBtn) {
                WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
                SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
            }
            bx += b.width + S(6);
        }
    }

    if (parent) EnableWindow(parent, FALSE);
    SetFocus(hStyleCtrl);

    MSG m;
    s_hrPropsDD.result = IDCANCEL;
    while (IsWindow(dlg) && GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { DestroyWindow(dlg); break; }
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    bool ok = (s_hrPropsDD.result == IDOK);
    if (s_hrSwatchBrush) { DeleteObject(s_hrSwatchBrush); s_hrSwatchBrush = NULL; }
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    if (ok) *inout = s_hrPropsResult;
    return ok;
}

// ── Ne_InsertHRule / Ne_EditHRuleProps (forward-declared near Ne_CaretOnHRule) ─
static void Ne_InsertHRule(HWND hwnd, HWND hEdit)
{
    NeHRuleProps p = { 0, 1, 60, 60, RGB(128,128,128), 100, 1 };  // defaults: single, 1pt, spacing, gray, full width, center
    if (!Ne_ShowHRulePropsDialog(hwnd, hEdit, true, &p)) return;

    // Collapse to end of current line
    CHARRANGE cr = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    LONG line = (LONG)SendMessageW(hEdit, EM_LINEFROMCHAR, cr.cpMax, 0);
    LONG len  = (LONG)SendMessageW(hEdit, EM_LINELENGTH,   cr.cpMax, 0);
    LONG idx  = (LONG)SendMessageW(hEdit, EM_LINEINDEX,    line, 0);
    cr.cpMin = cr.cpMax = idx + len;
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

    // Insert the rule paragraph
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"\r");
    Ne_ApplyHRuleProps(hEdit, p);

    // Move to a clean follow-up paragraph, stripping border inheritance
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"\r");
    PARAFORMAT2 pfClean = {}; pfClean.cbSize = sizeof(pfClean);
    pfClean.dwMask        = PFM_BORDER | PFM_STYLE | PFM_SPACEBEFORE | PFM_SPACEAFTER
                          | PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_ALIGNMENT;
    pfClean.wBorders      = 0;
    pfClean.sStyle        = 0;   // clear inherited HR marker
    pfClean.dySpaceBefore = 0;
    pfClean.dySpaceAfter  = 0;
    pfClean.dxStartIndent = 0;
    pfClean.dxRightIndent = 0;
    pfClean.wAlignment    = PFA_LEFT;
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pfClean);
    Ne_RebuildHRList(hEdit);
    InvalidateRect(hEdit, NULL, FALSE);
}

static void Ne_EditHRuleProps(HWND hwnd, HWND hEdit)
{
    NeHRuleProps p = Ne_ReadHRuleProps(hEdit);
    if (!Ne_ShowHRulePropsDialog(hwnd, hEdit, false, &p)) return;
    Ne_ApplyHRuleProps(hEdit, p);
    Ne_RebuildHRList(hEdit);
    InvalidateRect(hEdit, NULL, FALSE);
}

// ── Line Spacing dialog ───────────────────────────────────────────────────────
static void Ne_ShowLineSpaceDialog(HWND parent, HWND hEdit)
{
    HINSTANCE hi = GetModuleHandleW(NULL);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NsbLineSpClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) RegisterClassW(&wc);

    const int W = S(260), H = S(165);
    RECT pr = {}; if (parent) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2-W/2, y = (pr.top+pr.bottom)/2-H/2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NsbLineSpClass", Ls(L"DLG_LINESPACE"),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    HFONT hf = Ne_MakeDlgFont(dlg);
    RECT rc; GetClientRect(dlg, &rc);
    const int P = S(10), LH = S(24), CB = S(26);
    int y0 = P;

    // Determine current spacing to pre-check the right radio.
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_LINESPACING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    // PFM_LINESPACING: bLineSpacingRule 0=single,1=1.5,2=double,3=multiple,4=exact,5=at-least
    int curRule = pf.bLineSpacingRule; // 0=single,1=1.5×,2=double

    auto mkRdo = [&](int id, const wchar_t* t, bool chk) {
        HWND h = CreateWindowExW(0,L"BUTTON",t,
            WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|(y0==P ? WS_GROUP : 0),
            P, y0, rc.right-2*P, LH, dlg,(HMENU)(UINT_PTR)id,hi,NULL);
        if(hf) SendMessageW(h,WM_SETFONT,(WPARAM)hf,TRUE);
        if(chk) SendMessageW(h,BM_SETCHECK,BST_CHECKED,0);
        y0 += LH + S(4);
    };
    mkRdo(1001, Ls(L"RDO_LINESPACE_S"),  curRule == 0);
    mkRdo(1002, Ls(L"RDO_LINESPACE_15"), curRule == 1);
    mkRdo(1003, Ls(L"RDO_LINESPACE_D"),  curRule == 2);
    y0 += P;

    int bw = S(80);
    int bx = rc.right - P - bw;
    auto mkBtnL = [&](int id, const wchar_t* t, int xx, bool def_) {
        DWORD sty = WS_CHILD|WS_VISIBLE|(def_?BS_DEFPUSHBUTTON:BS_PUSHBUTTON);
        HWND h = CreateWindowExW(0,L"BUTTON",t,sty, xx,y0,bw,CB, dlg,(HMENU)(UINT_PTR)id,hi,NULL);
        if(hf) SendMessageW(h,WM_SETFONT,(WPARAM)hf,TRUE);
    };
    mkBtnL(IDCANCEL, Ls(L"BTN_CANCEL"), bx, false); bx -= bw + S(6);
    mkBtnL(IDOK,     Ls(L"BTN_SAVE"),   bx, true);

    if (parent) EnableWindow(parent, FALSE);

    MSG m; bool ok = false;
    while (IsWindow(dlg) && GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { DestroyWindow(dlg); break; }
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (!IsWindow(dlg)) break;
        if (m.message == WM_COMMAND && LOWORD(m.wParam) == IDOK && m.hwnd == dlg) { ok = true; break; }
    }
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }

    if (ok && IsWindow(dlg)) {
        int rule = 0;
        if (SendMessageW(GetDlgItem(dlg, 1002), BM_GETCHECK, 0, 0) == BST_CHECKED) rule = 1;
        if (SendMessageW(GetDlgItem(dlg, 1003), BM_GETCHECK, 0, 0) == BST_CHECKED) rule = 2;
        DestroyWindow(dlg);
        PARAFORMAT2 pf2 = {}; pf2.cbSize = sizeof(pf2);
        pf2.dwMask           = PFM_LINESPACING;
        pf2.bLineSpacingRule = (BYTE)rule;
        pf2.dyLineSpacing    = 0; // 0 = auto for rules 0,1,2
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf2);
    } else if (IsWindow(dlg)) {
        DestroyWindow(dlg);
    }
    SetFocus(hEdit);
}

// ── Paragraph Spacing dialog ──────────────────────────────────────────────────
static void Ne_ShowParSpaceDialog(HWND parent, HWND hEdit)
{
    HINSTANCE hi = GetModuleHandleW(NULL);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NsbParSpClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) RegisterClassW(&wc);

    const int W = S(300), H = S(155);
    RECT pr = {}; if (parent) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2-W/2, y = (pr.top+pr.bottom)/2-H/2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NsbParSpClass", Ls(L"DLG_PARSPACE"),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    HFONT hf = Ne_MakeDlgFont(dlg);
    RECT rc; GetClientRect(dlg, &rc);
    const int P = S(10), EB = S(22), LH = S(24), CB = S(26);
    int y0 = P;

    // Read current values (in twips; 1pt = 20 twips).
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_SPACEBEFORE | PFM_SPACEAFTER;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    int befPt = pf.dySpaceBefore / 20, aftPt = pf.dySpaceAfter / 20;

    auto mkLbl = [&](const wchar_t* t, int xx, int yy) {
        HWND h = CreateWindowExW(0,L"STATIC",t,WS_CHILD|WS_VISIBLE, xx,yy+S(3),S(130),LH,dlg,NULL,hi,NULL);
        if(hf) SendMessageW(h,WM_SETFONT,(WPARAM)hf,TRUE);
    };
    auto mkSpinP = [&](int id, int xx, int yy, int init) -> HWND {
        HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_CENTER,
            xx,yy,S(50),EB, dlg,(HMENU)(UINT_PTR)id,hi,NULL);
        if(hf) SendMessageW(hEd,WM_SETFONT,(WPARAM)hf,TRUE);
        wchar_t buf[8]; swprintf_s(buf,L"%d",init); SetWindowTextW(hEd,buf);
        HWND hSp = CreateWindowExW(0,UPDOWN_CLASSW,NULL,
            WS_CHILD|WS_VISIBLE|UDS_ALIGNRIGHT|UDS_SETBUDDYINT|UDS_ARROWKEYS,
            0,0,0,0, dlg,NULL,hi,NULL);
        SendMessageW(hSp,UDM_SETBUDDY,(WPARAM)hEd,0);
        SendMessageW(hSp,UDM_SETRANGE32,0,200);
        SendMessageW(hSp,UDM_SETPOS32,0,init);
        return hEd;
    };

    mkLbl(Ls(L"DLG_PARSPACE_BEF"), P, y0);
    HWND hBef = mkSpinP(IDC_NE_DLG_PAR_BEF, P+S(140), y0, befPt); y0 += EB + P;
    mkLbl(Ls(L"DLG_PARSPACE_AFT"), P, y0);
    HWND hAft = mkSpinP(IDC_NE_DLG_PAR_AFT, P+S(140), y0, aftPt); y0 += EB + P*2;

    int bw = S(80);
    int bx = rc.right - P - bw;
    auto mkBtnP = [&](int id, const wchar_t* t, int xx, bool def_) {
        DWORD sty = WS_CHILD|WS_VISIBLE|(def_?BS_DEFPUSHBUTTON:BS_PUSHBUTTON);
        HWND h = CreateWindowExW(0,L"BUTTON",t,sty, xx,y0,bw,CB, dlg,(HMENU)(UINT_PTR)id,hi,NULL);
        if(hf) SendMessageW(h,WM_SETFONT,(WPARAM)hf,TRUE);
    };
    mkBtnP(IDCANCEL, Ls(L"BTN_CANCEL"), bx, false); bx -= bw + S(6);
    mkBtnP(IDOK,     Ls(L"BTN_SAVE"),   bx, true);

    if (parent) EnableWindow(parent, FALSE);
    SetFocus(hBef);

    MSG m; bool ok = false;
    while (IsWindow(dlg) && GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { DestroyWindow(dlg); break; }
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (!IsWindow(dlg)) break;
        if (m.message == WM_COMMAND && LOWORD(m.wParam) == IDOK && m.hwnd == dlg) { ok = true; break; }
    }
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }

    if (ok && IsWindow(dlg)) {
        wchar_t bb[8]={}, ab[8]={};
        GetWindowTextW(hBef, bb, 8); GetWindowTextW(hAft, ab, 8);
        DestroyWindow(dlg);
        PARAFORMAT2 pf2 = {}; pf2.cbSize = sizeof(pf2);
        pf2.dwMask        = PFM_SPACEBEFORE | PFM_SPACEAFTER;
        pf2.dySpaceBefore = (LONG)(_wtoi(bb) * 20);
        pf2.dySpaceAfter  = (LONG)(_wtoi(ab) * 20);
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf2);
    } else if (IsWindow(dlg)) {
        DestroyWindow(dlg);
    }
    SetFocus(hEdit);
}

static void Ne_ShowShortcuts(HWND parent)
{
    // Simple modal dialog: title bar, a ListView, and a Close button.
    // We create it manually (no .rc dialog template needed).
    HINSTANCE hi = GetModuleHandleW(NULL);

    // ── Register class (once) ─────────────────────────────────────────────────
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL))
            { PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
        if (m == WM_CLOSE)  { DestroyWindow(h); return 0; }
        if (m == WM_DESTROY) {
            // Free the bold font we created for the Shortcut column.
            HFONT hf = (HFONT)GetWindowLongPtrW(h, GWLP_USERDATA);
            if (hf) DeleteObject(hf);
            PostQuitMessage(0); return 0;
        }
        if (m == WM_KEYDOWN && w == VK_ESCAPE) { PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
        // ── Custom-draw: bold Shortcut column, royal-blue Description column ──
        if (m == WM_NOTIFY) {
            NMHDR* hdr = (NMHDR*)l;
            if (hdr->idFrom == 100 && hdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)l;
                switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:
                    return CDRF_NOTIFYSUBITEMDRAW;
                case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
                    if (cd->iSubItem == 0) {
                        HFONT hfBold = (HFONT)GetWindowLongPtrW(h, GWLP_USERDATA);
                        if (hfBold) SelectObject(cd->nmcd.hdc, hfBold);
                        return CDRF_NEWFONT;
                    }
                    if (cd->iSubItem == 2) {
                        cd->clrText = RGB(0, 56, 184);  // dark royal blue
                        return CDRF_NEWFONT;
                    }
                    return CDRF_DODEFAULT;
                }
                }
            }
        }
        return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance     = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NsbShortcutsClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) RegisterClassW(&wc);

    const int W = S(640), H = S(520);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left + pr.right)  / 2 - W / 2;
    int y = (pr.top  + pr.bottom) / 2 - H / 2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"NsbShortcutsClass", Ls(L"SHORTCUTS_TITLE"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
        SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco);
    }

    RECT rcC; GetClientRect(dlg, &rcC);
    const int PAD   = S(8);
    const int BTN_H = S(28);

    // ── ListView ──────────────────────────────────────────────────────────────
    HWND hLV = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOSORTHEADER | LVS_SINGLESEL | WS_VSCROLL,
        PAD, PAD,
        rcC.right - 2 * PAD, rcC.bottom - 3 * PAD - BTN_H,
        dlg, (HMENU)100, hi, NULL);
    SendMessageW(hLV, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Create a bold font for the Shortcut column; store on the dialog for cleanup.
    HFONT hfBase = (HFONT)SendMessageW(hLV, WM_GETFONT, 0, 0);
    if (!hfBase) hfBase = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    LOGFONTW lfBold = {};
    GetObjectW(hfBase, sizeof(lfBold), &lfBold);
    lfBold.lfWeight = FW_BOLD;
    HFONT hfBold = CreateFontIndirectW(&lfBold);
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)hfBold);

    // Columns
    auto addCol = [&](int idx, const wchar_t* text, int w) {
        LVCOLUMNW lvc = {};
        lvc.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.iSubItem = idx;
        lvc.pszText  = (LPWSTR)text;
        lvc.cx       = w;
        ListView_InsertColumn(hLV, idx, &lvc);
    };
    addCol(0, Ls(L"SHORTCUTS_COL1"), S(170));
    addCol(1, Ls(L"SHORTCUTS_COL2"), S(130));
    addCol(2, Ls(L"SHORTCUTS_COL3"), S(295));

    // Rows
    for (int i = 0; i < s_shortcutCount; ++i) {
        LVITEMW lvi = {};
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = i;
        lvi.iSubItem = 0;
        lvi.pszText  = (LPWSTR)s_shortcuts[i].key;
        ListView_InsertItem(hLV, &lvi);
        ListView_SetItemText(hLV, i, 1, (LPWSTR)s_shortcuts[i].func);
        ListView_SetItemText(hLV, i, 2, (LPWSTR)s_shortcuts[i].desc);
    }

    // ── Close button ──────────────────────────────────────────────────────────
    int bx = (rcC.right - S(80)) / 2;
    int by = rcC.bottom - PAD - BTN_H;
    HWND hBtn = CreateWindowExW(0, L"BUTTON", Ls(L"SHORTCUTS_BTN_CLOSE"),
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        bx, by, S(80), BTN_H, dlg, (HMENU)IDOK, hi, NULL);
    SendMessageW(hBtn, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(hBtn);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE)
            PostMessageW(dlg, WM_CLOSE, 0, 0);
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
}

static bool Ne_IsRtf(const std::string& bytes)
{
    return bytes.size() >= 5 && bytes.compare(0, 5, "{\\rtf") == 0;
}

// ── Encoding helpers ──────────────────────────────────────────────────────────

// True when the active doc's path has a .rtf extension.
static bool Ne_DocIsRtf(NeTabDoc* doc)
{
    if (!doc || doc->path.empty()) return false;
    size_t dot = doc->path.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = doc->path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext == L"rtf";
}

// Return the status-bar label for a given NeEncoding.
static const wchar_t* Ne_EncLabel(NeEncoding enc)
{
    switch (enc) {
    case NeEncoding::RichText:  return Ls(L"ENC_RICHTEXT");
    case NeEncoding::UTF8:      return Ls(L"ENC_UTF8");
    case NeEncoding::UTF16LE:   return Ls(L"ENC_UTF16LE");
    case NeEncoding::ANSI:      return Ls(L"ENC_ANSI");
    case NeEncoding::Win1252:   return Ls(L"ENC_WIN1252");
    case NeEncoding::ISO8859_1: return Ls(L"ENC_ISO8859_1");
    default:                    return Ls(L"ENC_UNKNOWN");
    }
}

// Convert the UTF-16LE wide text from RichEdit back to a narrow encoding.
// codepage 0 = CP_ACP (ANSI), 1252 = Windows-1252, 28591 = ISO-8859-1.
static std::string Ne_WideToCodepage(const std::wstring& wide, UINT cp)
{
    int n = WideCharToMultiByte(cp, 0, wide.c_str(), (int)wide.size(), NULL, 0, "?", NULL);
    std::string out(n, '\0');
    WideCharToMultiByte(cp, 0, wide.c_str(), (int)wide.size(), out.data(), n, "?", NULL);
    return out;
}

// Detect encoding from raw file bytes. Sets doc->encoding.
static void Ne_DetectEncoding(NeTabDoc* doc, const std::string& bytes)
{
    if (!doc) return;
    if (Ne_IsRtf(bytes)) { doc->encoding = (int)NeEncoding::RichText; return; }
    const unsigned char* p = (const unsigned char*)bytes.data();
    size_t n = bytes.size();
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        doc->encoding = (int)NeEncoding::UTF16LE; return;
    }
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        doc->encoding = (int)NeEncoding::UTF8; return;
    }
    // Try strict UTF-8 decode.
    size_t offset = 0;
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   bytes.data() + offset, (int)(n - offset), NULL, 0);
    doc->encoding = (int)(wlen > 0 ? NeEncoding::UTF8 : NeEncoding::ANSI);
}

// ── Detect whether the document contains any non-plain formatting ─────────────
static bool Ne_HasFormatting(HWND hEdit)
{
    // Save & restore selection so the user doesn't notice.
    CHARRANGE crOld;
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&crOld);
    SendMessageW(hEdit, EM_SETSEL, 0, -1);

    // Character formatting common to the entire selection.
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // Paragraph formatting.
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);

    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crOld);

    // ── Character checks ──────────────────────────────────────────────────────
    // If a CFM_ bit is CLEAR the property is mixed across the document → formatting.
    const DWORD fmtBits = CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE|CFM_STRIKEOUT|
                          CFM_SUBSCRIPT|CFM_SUPERSCRIPT|CFM_COLOR|CFM_BACKCOLOR;
    if ((cf.dwMask & fmtBits) != fmtBits) return true;         // mixed

    // Uniform but active effects.
    if (cf.dwEffects & (CFE_BOLD|CFE_ITALIC|CFE_UNDERLINE|CFE_STRIKEOUT|
                        CFE_SUBSCRIPT|CFE_SUPERSCRIPT))         return true;

    // Non-auto colour or background colour.
    if (!(cf.dwEffects & CFE_AUTOCOLOR))                        return true;
    if (!(cf.dwEffects & CFE_AUTOBACKCOLOR))                    return true;

    // Mixed or non-default font size (240 half-pts = 12pt in RichEdit CHARFORMAT).
    if (!(cf.dwMask & CFM_SIZE))                                return true;
    if (cf.yHeight != 240)                                      return true;

    // ── Paragraph checks ─────────────────────────────────────────────────────
    if ((pf.dwMask & PFM_ALIGNMENT)   && pf.wAlignment != PFA_LEFT) return true;
    if ((pf.dwMask & PFM_NUMBERING)   && pf.wNumbering != 0)        return true;
    if ((pf.dwMask & PFM_OFFSET)      && pf.dxOffset != 0)          return true;
    if ((pf.dwMask & PFM_STARTINDENT) && pf.dxStartIndent != 0)     return true;
    if ((pf.dwMask & PFM_SPACEBEFORE) && pf.dySpaceBefore != 0)     return true;
    if ((pf.dwMask & PFM_SPACEAFTER)  && pf.dySpaceAfter  != 0)     return true;

    return false;
}

// ── HR metadata store ─────────────────────────────────────────────────────────
// Paragraph scanning with EM_EXSETSEL must NOT happen inside WM_PAINT (triggers
// re-entrant WM_PAINT via RichEdit internal redraws).  We scan once on EN_CHANGE
// and cache results; WM_PAINT only calls EM_POSFROMCHAR (safe, no sel change).
struct NeHRuleEntry {
    LONG     charIdx;
    COLORREF color;
    int      thicknessTwips;   // pt * 20
    int      leftIndentTwips;
    int      rightIndentTwips;
};
static std::map<HWND, std::vector<NeHRuleEntry>> g_hrMap;

// Rebuild HR list for hEdit.  Safe to call at any time except during WM_PAINT.
static void Ne_RebuildHRList(HWND hEdit)
{
    auto& list = g_hrMap[hEdit];
    list.clear();

    int lineCount = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
    if (lineCount <= 0) return;

    CHARRANGE crSave = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&crSave);
    DWORD oldMask = (DWORD)SendMessageW(hEdit, EM_GETEVENTMASK, 0, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);  // suppress visual selection flicker during scan
    for (int ln = 0; ln < lineCount; ln++) {
        LONG idx = (LONG)SendMessageW(hEdit, EM_LINEINDEX, ln, 0);
        if (idx < 0) break;
        CHARRANGE cr = { idx, idx };
        SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

        PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_BORDER | PFM_STYLE | PFM_STARTINDENT | PFM_RIGHTINDENT;
        SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
        // Detect by sStyle marker (most reliable) or legacy wBorders
        if (pf.sStyle != 42 && !pf.wBorders) continue;

        CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

        NeHRuleEntry e;
        e.charIdx          = idx;
        e.color            = (cf.dwEffects & CFE_AUTOCOLOR) ? RGB(128,128,128) : cf.crTextColor;
        e.thicknessTwips   = (int)pf.wBorderWidth;
        e.leftIndentTwips  = (int)pf.dxStartIndent;
        e.rightIndentTwips = (int)pf.dxRightIndent;
        list.push_back(e);
    }

    SendMessageW(hEdit, EM_EXSETSEL,    0, (LPARAM)&crSave);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, oldMask);
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
}

// Draw cached HR lines — NO selection changes here.
static void Ne_PaintHRules(HWND hwnd, HDC hdc)
{
    auto it = g_hrMap.find(hwnd);
    if (it == g_hrMap.end() || it->second.empty()) return;

    RECT rcC;  GetClientRect(hwnd, &rcC);
    RECT fmtRc = {}; SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&fmtRc);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);

    for (const auto& e : it->second) {
        // Screen position — EM_POSFROMCHAR does NOT change the selection
        POINTL ptLine = {};
        SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&ptLine, (LPARAM)e.charIdx);
        int lineY = ptLine.y;

        // Line height
        int lineH = S(20);
        int curLn = (int)SendMessageW(hwnd, EM_LINEFROMCHAR, e.charIdx, 0);
        LONG nextIdx = (LONG)SendMessageW(hwnd, EM_LINEINDEX, curLn + 1, 0);
        if (nextIdx > e.charIdx) {
            POINTL ptNext = {};
            SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&ptNext, (LPARAM)nextIdx);
            if (ptNext.y > lineY) lineH = ptNext.y - lineY;
        }

        int leftPx  = fmtRc.left  + MulDiv(e.leftIndentTwips,  dpiX, 1440);
        int rightPx = fmtRc.right - MulDiv(e.rightIndentTwips, dpiX, 1440);
        int drawY   = lineY + lineH / 2;

        int penPx = std::max(1, MulDiv(e.thicknessTwips, dpiY, 1440));
        LOGBRUSH lb = { BS_SOLID, e.color, 0 };
        HPEN hPen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT, penPx, &lb, 0, NULL);
        if (!hPen) hPen = CreatePen(PS_SOLID, penPx, e.color);
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, leftPx,  drawY, NULL);
        LineTo  (hdc, rightPx, drawY);
        SelectObject(hdc, hOld);
        DeleteObject(hPen);
    }
}

// ── Hyperlink URL extraction from RTF field instruction ─────────────────────
// RichEdit HYPERLINK fields store the URL in hidden \fldinst text.
// EM_GETTEXTRANGE gives display text only; we must stream out RTF and parse.
static std::wstring Ne_ParseHyperlinkFromRtf(const std::string& rtf)
{
    size_t pos = rtf.find("HYPERLINK");
    if (pos == std::string::npos) return {};
    pos += 9;
    while (pos < rtf.size() && rtf[pos] == ' ') pos++;
    if (pos >= rtf.size() || rtf[pos] != '"') return {};
    ++pos;
    size_t end = rtf.find('"', pos);
    if (end == std::string::npos) return {};
    std::string urlA = rtf.substr(pos, end - pos);
    int n = MultiByteToWideChar(CP_ACP, 0, urlA.c_str(), (int)urlA.size(), NULL, 0);
    if (n <= 0) return {};
    std::wstring url(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, urlA.c_str(), (int)urlA.size(), &url[0], n);
    return url;
}

// Extract hyperlink URL at charIdx by streaming out the surrounding RTF.
// The field instruction (hidden text) precedes the visible display text;
// going back 512 chars is enough to capture any realistic URL.
static std::wstring Ne_ExtractLinkUrlAt(HWND hEdit, LONG charIdx)
{
    CHARRANGE crSav = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&crSav);

    int docLen = GetWindowTextLengthW(hEdit);
    LONG cpMin = std::max(0L, charIdx - 512);
    LONG cpMax = std::min((LONG)docLen, charIdx + 1);
    CHARRANGE cr = { cpMin, cpMax };
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

    std::string rtf;
    EDITSTREAM es = { (DWORD_PTR)&rtf, 0, Ne_WriteCb };
    SendMessageW(hEdit, EM_STREAMOUT, SF_RTF | SFF_SELECTION, (LPARAM)&es);
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crSav);

    return Ne_ParseHyperlinkFromRtf(rtf);
}

// ── Thick-caret subclass for main editor RichEdit(s) ──────────────────────
static LRESULT CALLBACK Ne_EditCaretProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC prev = (WNDPROC)GetPropW(hwnd, L"NeEditCaretPrev");
    if (msg == WM_SETFOCUS) {
        LRESULT r = CallWindowProcW(prev, hwnd, msg, wParam, lParam);
        // Recreate the caret with a visible 2-pixel width, matching the editor font height.
        TEXTMETRICW tm = {};
        HDC hdc = GetDC(hwnd);
        HFONT hf = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        HFONT hfOld = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
        GetTextMetricsW(hdc, &tm);
        if (hfOld) SelectObject(hdc, hfOld);
        ReleaseDC(hwnd, hdc);
        DestroyCaret();
        CreateCaret(hwnd, NULL, S(2), tm.tmHeight);
        ShowCaret(hwnd);
        return r;
    }
    if (msg == WM_MOUSEMOVE) {
        // Call through first so RichEdit sets its cursor (IDC_HAND over links).
        LRESULT r = CallWindowProcW(prev, hwnd, msg, wParam, lParam);
        static HCURSOR s_hHand = LoadCursor(NULL, IDC_HAND);
        static bool s_wasLink  = false;
        bool isLink = (GetCursor() == s_hHand);
        if (isLink && !s_wasLink) {
            // Cursor just entered a link — extract URL and show two-line tooltip.
            POINT ptClient = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            LRESULT idx = SendMessageW(hwnd, EM_CHARFROMPOS, 0, (LPARAM)&ptClient);
            std::wstring url = Ne_ExtractLinkUrlAt(hwnd, (LONG)idx);
            POINT ptScreen; GetCursorPos(&ptScreen);
            std::vector<TooltipEntry> tips = {
                { L"", url.empty() ? std::wstring(L"") : url },
                { L"", Ls(L"LINK_TIP_CTRL") }
            };
            ShowMultilingualTooltip(tips, ptScreen.x, ptScreen.y + S(20), GetParent(hwnd));
        } else if (!isLink && IsTooltipVisible()) {
            HideTooltip();
        }
        s_wasLink = isLink;
        return r;
    }
    if (msg == WM_MOUSELEAVE) {
        HideTooltip();
    }
    if (msg == WM_LBUTTONDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        // Ctrl+Click on a hyperlink — extract URL and open in browser.
        static HCURSOR s_hHandC = LoadCursor(NULL, IDC_HAND);
        if (GetCursor() == s_hHandC) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            LRESULT idx = SendMessageW(hwnd, EM_CHARFROMPOS, 0, (LPARAM)&pt);
            std::wstring url = Ne_ExtractLinkUrlAt(hwnd, (LONG)idx);
            if (!url.empty()) {
                HideTooltip();
                ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return 0;
            }
        }
    }
    if (msg == WM_PAINT) {
        // Let RichEdit handle its full BeginPaint/EndPaint cycle first.
        LRESULT r = CallWindowProcW(prev, hwnd, msg, wParam, lParam);
        // Then overlay HR lines with a fresh DC (avoids nested-BeginPaint clip issues).
        HDC hdc = GetDC(hwnd);
        Ne_PaintHRules(hwnd, hdc);
        ReleaseDC(hwnd, hdc);
        return r;
    }
    if (msg == WM_NCDESTROY) {
        g_hrMap.erase(hwnd);
        RemovePropW(hwnd, L"NeEditCaretPrev");
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)prev);
    }
    return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
}

static void Ne_SubclassEditForCaret(HWND hEdit)
{
    if (!hEdit || GetPropW(hEdit, L"NeEditCaretPrev")) return; // already subclassed
    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)Ne_EditCaretProc);
    SetPropW(hEdit, L"NeEditCaretPrev", (HANDLE)prev);
}

static void Ne_New(HWND hwnd)
{
    if (!NeTabs_AddUntitled(hwnd)) return;
    Ne_UpdateTitle(hwnd);
    Ne_UpdateStatusText(hwnd);
    HWND hEdit = NeTabs_GetActiveEdit(hwnd);
    if (hEdit) {
        CHARFORMAT2W cfD = {}; cfD.cbSize = sizeof(cfD);
        cfD.dwMask    = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR | CFM_EFFECTS;
        cfD.dwEffects = CFE_AUTOCOLOR;
        cfD.yHeight   = s_neFontSizes[s_neFontDefault] * 20;
        cfD.bCharSet  = DEFAULT_CHARSET;
        wcsncpy_s(cfD.szFaceName, L"Segoe UI", LF_FACESIZE - 1);
        SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfD);
        SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_LINK | ENM_SCROLL);
        Ne_SubclassEditForCaret(hEdit);
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_SELCHANGE);  // suppress EN_CHANGE during attach
        Ne_AttachScrollbars(hEdit);
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_LINK | ENM_SCROLL);
        SetFocus(hEdit);
    }
    NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
    Ne_SyncScrollbarVisibility(hwnd);
    Ne_SyncRichGutters(hwnd);
    // Apply saved zoom to the new RTF tab.
    if (hEdit) SendMessageW(hEdit, EM_SETZOOM, (WPARAM)g_zoomRtf, 100);
}

static bool Ne_LoadPathIntoEditor(HWND hwnd, const std::wstring& path)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string bytes((size_t)std::max(0L, sz), '\0');
    if (sz > 0) fread(&bytes[0], 1, (size_t)sz, f);
    fclose(f);

    HWND hEdit = NeTabs_GetActiveEdit(hwnd);
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (!hEdit || !doc) return false;

    bool isRtf = Ne_IsRtf(bytes);

    if (!isRtf) {
        // ── Non-RTF file: use Scintilla ─────────────────────────────────────
        // Convert bytes to UTF-8 for Scintilla.
        std::string utf8;
        {
            const unsigned char* p = (const unsigned char*)bytes.data();
            size_t n = bytes.size();
            // Already UTF-8 (with or without BOM)?
            size_t offset = (n >= 3 && p[0]==0xEF && p[1]==0xBB && p[2]==0xBF) ? 3 : 0;
            int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           bytes.data() + offset, (int)(n - offset), NULL, 0);
            std::wstring wide;
            if (n >= 2 && p[0]==0xFF && p[1]==0xFE) {
                // UTF-16LE BOM
                const wchar_t* wp = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
                int wn = (int)((n - 2) / sizeof(wchar_t));
                wide.assign(wp, wn);
            } else if (wlen > 0) {
                wide.resize(wlen);
                MultiByteToWideChar(CP_UTF8, 0, bytes.data() + offset, (int)(n - offset),
                                    wide.data(), wlen);
            } else {
                wlen = MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)n, NULL, 0);
                if (wlen > 0) {
                    wide.resize(wlen);
                    MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)n, wide.data(), wlen);
                }
            }
            // Wide → UTF-8
            if (!wide.empty()) {
                int u8len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                                NULL, 0, NULL, NULL);
                if (u8len > 0) {
                    utf8.resize(u8len);
                    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                        utf8.data(), u8len, NULL, NULL);
                }
            }
        }

        // Create Scintilla window if this tab doesn't have one yet.
        if (!doc->hSci) {
            RECT rcEdit; GetWindowRect(hEdit, &rcEdit);
            POINT pt = { rcEdit.left, rcEdit.top };
            ScreenToClient(hwnd, &pt);
            int w = rcEdit.right - rcEdit.left;
            int h = rcEdit.bottom - rcEdit.top;
            doc->hSci = Ne_CreateScintilla(hwnd, pt.x, pt.y, w, h);
        }

        if (!doc->hSci) return false;

        // Detect language from extension (unless user has overridden it).
        if (!doc->langUserSet)
            doc->langId = Ne_LangFromExt(path);

        Ne_ApplyLang(doc->hSci, doc->langId);

        // Load text (UTF-8 is Scintilla's native encoding).
        SendMessageW(doc->hSci, SCI_SETTEXT, 0, (LPARAM)utf8.c_str());
        SendMessageW(doc->hSci, SCI_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(doc->hSci, SCI_SETSAVEPOINT, 0, 0);

        // Hide the RichEdit, show Scintilla, focus it.
        ShowWindow(hEdit, SW_HIDE);
        ShowWindow(doc->hSci, SW_SHOW);
        SetFocus(doc->hSci);
        // Apply saved zoom to the Scintilla tab.
        SendMessageW(doc->hSci, SCI_SETZOOM, (WPARAM)g_zoomSci, 0);
        // Hide the RichEdit gutter (Scintilla has its own built-in margin).
        Ne_SyncRichGutters(hwnd);

        doc->path = path;
        doc->modified = false;
        Ne_DetectEncoding(doc, bytes);
        Ne_RememberDiskStamp(doc);
        NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
        Ne_UpdateTitle(hwnd);
        Ne_UpdateStatusText(hwnd);
        Ne_UpdateLangMenuCheck(doc->langId);
        return true;
    }

    // ── RTF file: use RichEdit ────────────────────────────────────────────────
    // If the tab was previously a Scintilla tab, destroy it.
    if (doc->hSci) {
        DestroyWindow(doc->hSci);
        doc->hSci = NULL;
        doc->langId = -1;
        doc->langUserSet = false;
    }
    ShowWindow(hEdit, SW_SHOW);
    // Apply saved zoom to the RTF tab.
    SendMessageW(hEdit, EM_SETZOOM, (WPARAM)g_zoomRtf, 100);

    std::string streamBytes = bytes;
    doc->suppressChange = true;
    Ne_StreamIn(hEdit, streamBytes, true);
    doc->suppressChange = false;
    Ne_RebuildHRList(hEdit);
    InvalidateRect(hEdit, NULL, FALSE);
    // Ensure gutter exists, is positioned, and repaints with the new content.
    Ne_SyncRichGutters(hwnd);
    doc->path = path;
    doc->modified = false;
    Ne_DetectEncoding(doc, bytes);
    Ne_RememberDiskStamp(doc);

    NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
    Ne_UpdateTitle(hwnd);
    Ne_UpdateStatusText(hwnd);
    Ne_UpdateLangMenuCheck(-1);
    return true;
}

static void Ne_Open(HWND hwnd)
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    auto filtOpen = Ne_Filter(L"FILTER_OPEN");
    ofn.lpstrFilter = filtOpen.c_str();
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = Ls(L"DLG_OPEN");
    if (!GetOpenFileNameW(&ofn)) return;

    if (!NeTabs_AddUntitled(hwnd)) return;

    if (!Ne_LoadPathIntoEditor(hwnd, path)) {
        MessageBoxW(hwnd, Ls(L"MSG_OPEN_ERR"), Ls(L"APP_NAME"), MB_OK | MB_ICONERROR);
        return;
    }

    HWND hEdit = NeTabs_GetActiveEdit(hwnd);
    if (hEdit) SetFocus(hEdit);
}

static bool Ne_SaveToPath(HWND hwnd, const std::wstring& path)
{
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (!doc) return false;

    // ── Scintilla tab: save UTF-8 directly ───────────────────────────────────
    if (doc->hSci) {
        std::string utf8 = Ne_SciGetText(doc->hSci);
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            MessageBoxW(hwnd, Ls(L"MSG_SAVE_ERR"), Ls(L"APP_NAME"), MB_OK | MB_ICONERROR);
            return false;
        }
        fwrite(utf8.data(), 1, utf8.size(), f);
        fclose(f);
        doc->path = path;
        doc->modified = false;
        doc->encoding = (int)NeEncoding::UTF8;
        SendMessageW(doc->hSci, SCI_SETSAVEPOINT, 0, 0);
        Ne_RememberDiskStamp(doc);
        NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
        Ne_UpdateTitle(hwnd);
        Ne_UpdateStatusText(hwnd);
        return true;
    }

    // ── RichEdit tab: existing save logic ────────────────────────────────────
    HWND hEdit = NeTabs_GetActiveEdit(hwnd);
    if (!hEdit) return false;

    bool asRtf = true;
    std::wstring savePath = path;
    size_t dot = savePath.rfind(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = savePath.substr(dot + 1);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        if (ext == L"txt" || ext == L"md") asRtf = false;
    }

    // ── Formatting-in-plain-text guard ────────────────────────────────────────
    if (!asRtf && Ne_HasFormatting(hEdit)) {
        int r = MessageBoxW(hwnd, Ls(L"MSG_HAS_FORMATTING"), Ls(L"APP_NAME"),
                            MB_YESNOCANCEL | MB_ICONWARNING);
        if (r == IDCANCEL) return false;
        if (r == IDNO) {
            // Redirect to RTF: replace extension with .rtf.
            savePath = (dot != std::wstring::npos ? savePath.substr(0, dot) : savePath) + L".rtf";
            asRtf = true;
            // Update the document's stored path so subsequent Ctrl+S goes to the .rtf.
            NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
            if (doc) doc->path = savePath;
        }
        // IDYES → fall through and save as plain text, losing formatting.
    }

    std::string bytes = Ne_StreamOut(hEdit, asRtf);

    // For plain text, Ne_StreamOut gives UTF-16LE bytes; convert to chosen encoding.
    std::string writeBytes;
    if (!asRtf) {
        const wchar_t* wptr = reinterpret_cast<const wchar_t*>(bytes.data());
        int wlen = (int)(bytes.size() / sizeof(wchar_t));
        // Determine target codepage from doc->encoding.
        NeTabDoc* docEnc = NeTabs_GetActiveDoc(hwnd);
        NeEncoding enc = docEnc ? (NeEncoding)docEnc->encoding : NeEncoding::UTF8;
        UINT cp = CP_UTF8;
        if      (enc == NeEncoding::UTF16LE)   cp = 1200;
        else if (enc == NeEncoding::ANSI)      cp = CP_ACP;
        else if (enc == NeEncoding::Win1252)   cp = 1252;
        else if (enc == NeEncoding::ISO8859_1) cp = 28591;

        if (cp == 1200) {
            // UTF-16LE: write raw wide chars (no BOM, matching what we load).
            writeBytes.assign(reinterpret_cast<const char*>(wptr), (size_t)wlen * sizeof(wchar_t));
        } else {
            int n = WideCharToMultiByte(cp, 0, wptr, wlen, NULL, 0, "?", NULL);
            if (n > 0) {
                writeBytes.resize(n);
                WideCharToMultiByte(cp, 0, wptr, wlen, writeBytes.data(), n, "?", NULL);
            }
        }
    } else {
        writeBytes = bytes;
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, savePath.c_str(), L"wb") != 0 || !f) {
        MessageBoxW(hwnd, Ls(L"MSG_SAVE_ERR"), Ls(L"APP_NAME"), MB_OK | MB_ICONERROR);
        return false;
    }
    fwrite(writeBytes.c_str(), 1, writeBytes.size(), f);
    fclose(f);

    // Update stored encoding.
    {
        NeTabDoc* doc2 = NeTabs_GetActiveDoc(hwnd);
        if (doc2) {
            if (asRtf) doc2->encoding = (int)NeEncoding::RichText;
            // else: encoding was already set by the user choice or detected on load;
            //       keep it unless it was Unknown.
            else if ((NeEncoding)doc2->encoding == NeEncoding::Unknown)
                doc2->encoding = (int)NeEncoding::UTF8;
        }
    }

    // If we just saved an RTF and it was converted from a plain text file,
    // offer to delete the original.
    if (asRtf) {
        NeTabDoc* doc3 = NeTabs_GetActiveDoc(hwnd);
        if (doc3 && !doc3->prevPlainPath.empty()) {
            std::wstring prevPath = doc3->prevPlainPath;
            doc3->prevPlainPath.clear(); // clear regardless of answer
            std::wstring msg = std::wstring(Ls(L"MSG_DEL_PLAIN_FILE"))
                               + L"\n\n" + prevPath;
            if (MessageBoxW(hwnd, msg.c_str(), Ls(L"APP_NAME"),
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DeleteFileW(prevPath.c_str());
            }
        }
    }

    // If we redirected to a different path (e.g. plain→rtf), refresh the UI.
    if (savePath != path) {
        NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
        Ne_UpdateTitle(hwnd);
    }
    return true;
}

static bool Ne_SaveAs(HWND hwnd)
{
    wchar_t path[MAX_PATH] = {};
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (doc && !doc->path.empty())
        wcsncpy_s(path, doc->path.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    auto filtSave = Ne_Filter(L"FILTER_SAVEAS");
    ofn.lpstrFilter = filtSave.c_str();
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"rtf";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = Ls(L"DLG_SAVEAS");
    if (!GetSaveFileNameW(&ofn)) return false;

    if (!Ne_SaveToPath(hwnd, path)) return false;
    if (doc) {
        doc->path = path;
        doc->modified = false;
        Ne_RememberDiskStamp(doc);
    }
    NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
    Ne_UpdateTitle(hwnd);
    Ne_UpdateStatusText(hwnd);
    return true;
}

static bool Ne_Save(HWND hwnd)
{
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (!doc) return false;

    // ── FTP/SFTP remote file: save locally then upload ─────────────────────
    if (doc->isFtpFile && !doc->ftpRemotePath.empty()) {
        if (doc->path.empty()) return Ne_SaveAs(hwnd);
        if (!Ne_SaveToPath(hwnd, doc->path)) return false;
        if (!NeFtp_SetActiveConn(doc->ftpProfileId)) {
            NeDialogButtonSpec btn = { IDOK, Ls(L"BTN_OK"), NeBtnTone::Red, IDI_ERROR, 0 };
            Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_CONN_FAILED"),
                                Ls(L"FTP_STATUS"), &btn, 1, IDOK);
            return false;
        }
        SetCursor(LoadCursorW(NULL, IDC_WAIT));
        bool ok = NeFtp_Upload(doc->path, doc->ftpRemotePath);
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
        if (!ok) {
            NeDialogButtonSpec btn = { IDOK, Ls(L"BTN_OK"), NeBtnTone::Red, IDI_ERROR, 0 };
            Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_CONN_FAILED"),
                                NeFtp_GetLastError(), &btn, 1, IDOK);
            return false;
        }
        doc->modified = false;
        NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
        Ne_UpdateTitle(hwnd);
        // ── Success notification ──────────────────────────────────────────
        NeDialogButtonSpec okBtn = { IDOK, Ls(L"BTN_OK"), NeBtnTone::Green,
                                     IDI_INFORMATION, 0 };
        Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_STATUS"),
                            Ls(L"MSG_FTP_UPLOAD_OK"), &okBtn, 1, IDOK);
        return true;
    }

    if (doc->path.empty()) return Ne_SaveAs(hwnd);
    if (!Ne_SaveToPath(hwnd, doc->path)) return false;
    doc->modified = false;
    Ne_RememberDiskStamp(doc);
    NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
    Ne_UpdateTitle(hwnd);
    return true;
}

static bool Ne_PromptSaveIfModified(HWND hwnd)
{
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (!doc || !doc->modified) return true;
    const wchar_t* name = doc->path.empty() ? Ls(L"UNTITLED") : doc->path.c_str();
    wchar_t msg[MAX_PATH + 64];
    swprintf_s(msg, MAX_PATH + 64, Ls(L"MSG_SAVE_PROMPT"), name);
    NeDialogButtonSpec btns[3] = {
        { IDYES,    Ls(L"BTN_SAVE"),       NeBtnTone::Green, IDI_INFORMATION, 0 },
        { IDNO,     Ls(L"BTN_DONT_SAVE"),  NeBtnTone::Red,   IDI_WARNING,     0 },
        { IDCANCEL, Ls(L"BTN_CANCEL"),     NeBtnTone::Blue,  IDI_ERROR,       0 },
    };
    int r = Ne_ShowChoiceDialog(hwnd, Ls(L"DLG_SAVE_CHANGES"), msg, btns, 3, IDCANCEL);
    if (r == IDCANCEL) return false;
    if (r == IDYES)    return Ne_Save(hwnd);
    return true; // IDNO — discard
}

static bool Ne_CloseTabAt(HWND hwnd, int index)
{
    if (!NeTabs_SetActive(hwnd, index)) return false;
    Ne_UpdateStatusText(hwnd);
    Ne_UpdateTitle(hwnd);

    if (!Ne_PromptSaveIfModified(hwnd)) return false;

    // Detach scrollbars before the edit HWND is destroyed
    HWND hEditPre = NeTabs_GetActiveEdit(hwnd);
    Ne_DetachScrollbars(hEditPre);

    if (!NeTabs_CloseTab(hwnd, index)) return false;

    Ne_SyncScrollbarVisibility(hwnd);
    Ne_UpdateStatusText(hwnd);
    Ne_UpdateTitle(hwnd);
    HWND hEdit = NeTabs_GetActiveEdit(hwnd);
    if (hEdit) Ne_SyncToolbar(hwnd, hEdit);
    return true;
}

static bool Ne_CloseAllTabsForExit(HWND hwnd)
{
    while (NeTabs_GetCount(hwnd) > 0) {
        int idx = NeTabs_GetActiveIndex(hwnd);
        if (idx < 0) break;
        if (!Ne_CloseTabAt(hwnd, idx)) return false;
        if (NeTabs_GetCount(hwnd) == 1) {
            NeTabDoc* d = NeTabs_GetActiveDoc(hwnd);
            if (d && d->path.empty() && !d->modified) break;
        }
    }
    return true;
}

// ── Tooltip subclass (English-only, project tooltip system) ───────────────────
static bool s_neTipTracking = false;
static HWND  s_neTipHwnd    = NULL;

static LRESULT CALLBACK Ne_TipSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC prev = (WNDPROC)(LONG_PTR)GetPropW(hwnd, L"neTipProc");
    if (!prev) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_MOUSEMOVE:
        if (!IsTooltipVisible() || s_neTipHwnd != hwnd) {
            const wchar_t* txt = (const wchar_t*)(void*)GetPropW(hwnd, L"neTipText");
            if (txt && *txt) {
                RECT rc; GetWindowRect(hwnd, &rc);
                std::vector<TooltipEntry> entries = { {L"", txt} };
                ShowMultilingualTooltip(entries, rc.left, rc.bottom + 4, GetParent(hwnd));
                s_neTipHwnd = hwnd;
            }
        }
        if (!s_neTipTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            s_neTipTracking = true;
        }
        break;
    case WM_MOUSELEAVE:
        HideTooltip();
        s_neTipHwnd     = NULL;
        s_neTipTracking = false;
        break;
    case WM_NCDESTROY: {
        wchar_t* tipCopy = (wchar_t*)(void*)GetPropW(hwnd, L"neTipText");
        delete[] tipCopy;
        RemovePropW(hwnd, L"neTipProc");
        RemovePropW(hwnd, L"neTipText");
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)prev);
        return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
    }
    }
    return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
}

static void Ne_SetTip(HWND hCtrl, const wchar_t* text)
{
    // Heap-copy so the pointer stays valid for the button's lifetime.
    // Freed in Ne_TipSubclassProc WM_NCDESTROY.
    size_t len = wcslen(text) + 1;
    wchar_t* copy = new wchar_t[len];
    wcscpy(copy, text);
    SetPropW(hCtrl, L"neTipText", (HANDLE)(void*)copy);
    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hCtrl, GWLP_WNDPROC, (LONG_PTR)Ne_TipSubclassProc);
    SetPropW(hCtrl, L"neTipProc", (HANDLE)(void*)(LONG_PTR)prev);
}

// ── Responsive toolbar layout ─────────────────────────────────────────────────
// Controls are placed left-to-right.  When the next one would overflow the row
// it drops to the next row — one control at a time.  When a row would hold
// fewer than 3 controls, the window can no longer be narrowed; that minimum
// width is stored in NeState::minClientW so WM_GETMINMAXINFO can enforce it.
// Returns the Y coordinate where the RichEdit should start.
static int Ne_LayoutToolbar(HWND hwnd, int cW, int topY)
{
    const int pad   = S(8);
    const int bSz   = S(26);
    const int bG    = S(3);
    const int sG    = S(8);
    const int wXs   = bSz + S(4);
    const int wAl   = bSz + S(4);
    const int wCol  = bSz + S(10);
    const int wFace = S(170);
    const int wSize = S(56);
    const int rowH  = bSz + S(4);   // height of one toolbar row including gap

    // Ordered list of all 17 toolbar controls: { id, pixel-width, gap-after }.
    // Gap conventions: bG = tight gap inside a group, sG = wider gap between groups.
    struct Ctrl { int id, w, gap; };
    const Ctrl ctrls[] = {
        { IDC_NE_BOLD,        bSz,   bG },
        { IDC_NE_ITALIC,      bSz,   bG },
        { IDC_NE_UNDERLINE,   bSz,   bG },
        { IDC_NE_STRIKE,      bSz,   sG },
        { IDC_NE_SUBSCRIPT,   wXs,   bG },
        { IDC_NE_SUPERSCRIPT, wXs,   sG },
        { IDC_NE_FONTFACE,    wFace, bG },
        { IDC_NE_FONTSIZE,    wSize, sG },
        { IDC_NE_ALIGN_L,     wAl,   bG },
        { IDC_NE_ALIGN_C,     wAl,   bG },
        { IDC_NE_ALIGN_R,     wAl,   bG },
        { IDC_NE_ALIGN_J,     wAl,   sG },
        { IDC_NE_BULLET,      wAl,   bG },
        { IDC_NE_NUMBERED,    wAl,   sG },
        { IDC_NE_COLOR,       wCol,  bG },
        { IDC_NE_HIGHLIGHT,   wCol,  sG },
        { IDC_NE_IMAGE,       wCol,  sG },
        // ── Indent
        { IDC_NE_INDENT_IN,   wXs,   bG },
        { IDC_NE_INDENT_OUT,  wXs,   sG },
        // ── Line / paragraph spacing
        { IDC_NE_LINESPACE,   wXs,   bG },
        { IDC_NE_PARSPACE,    wXs,   sG },
        // ── Find & insert
        { IDC_NE_FIND,        wCol,  bG },
        { IDC_NE_LINK,        wCol,  bG },
        { IDC_NE_TABLE,      wCol - S(13), 0 },
        { IDC_NE_TABLE_DROP, S(13),           bG },
        { IDC_NE_HLINE,       wCol,  sG },
        // ── Clear / print
        { IDC_NE_CLEARFMT,    wCol,  bG },
        { IDC_NE_PRINT_BTN,   wCol,  sG },
        // ── Zoom / wrap / case
        { IDC_NE_ZOOM,        S(72), bG },
        { IDC_NE_WORDWRAP,    wXs,   bG },
        { IDC_NE_CASE,        wXs,   0  },
    };
    const int N = (int)(sizeof(ctrls) / sizeof(ctrls[0]));

    // ── Pass 1: assign each control to a row ─────────────────────────────────
    // Row changes when adding the next control would push x past cW - pad.
    int rowOf[N];        // which row each control belongs to
    int rowCount[32] = {};  // number of controls per row (unlikely > 32 rows)
    int numRows = 0;
    {
        int x = pad;
        int row = 0;
        rowCount[0] = 0;
        for (int i = 0; i < N; ++i) {
            int needed = ctrls[i].w + (i + 1 < N ? ctrls[i].gap : 0);
            if (rowCount[row] > 0 && x + needed > cW - pad) {
                // wrap to next row
                ++row;
                x = pad;
                rowCount[row] = 0;
            }
            rowOf[i]    = row;
            rowCount[row]++;
            x += ctrls[i].w + ctrls[i].gap;
            if (row + 1 > numRows) numRows = row + 1;
        }
    }

    // ── Pass 2: place controls ────────────────────────────────────────────────
    int x   = pad;
    int curRow = 0;
    int y   = topY;
    for (int i = 0; i < N; ++i) {
        if (rowOf[i] != curRow) {
            curRow = rowOf[i];
            x = pad;
            y = topY + curRow * rowH;
        }
        HWND h = GetDlgItem(hwnd, ctrls[i].id);
        if (h) SetWindowPos(h, NULL, x, y, ctrls[i].w, bSz, SWP_NOZORDER | SWP_NOACTIVATE);
        x += ctrls[i].w + ctrls[i].gap;
    }

    // ── Compute minimum client width ──────────────────────────────────────────
    // Allow narrowing until at most 3 controls fit per row.
    // Minimum = padding + (3 widest controls + their inter-gaps).
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (st) {
        // Collect the 3 largest widths (including their right-gap so the
        // greedy packing check is consistent).
        int top3w[3] = {0, 0, 0};
        int top3g[3] = {0, 0, 0};
        for (int i = 0; i < N; ++i) {
            int w = ctrls[i].w;
            int g = ctrls[i].gap;
            if (w > top3w[0]) { top3w[2]=top3w[1]; top3g[2]=top3g[1]; top3w[1]=top3w[0]; top3g[1]=top3g[0]; top3w[0]=w; top3g[0]=g; }
            else if (w > top3w[1]) { top3w[2]=top3w[1]; top3g[2]=top3g[1]; top3w[1]=w; top3g[1]=g; }
            else if (w > top3w[2]) { top3w[2]=w; top3g[2]=g; }
        }
        // Width needed to fit those 3 items (no trailing gap on the last one).
        st->minClientW = pad + top3w[0] + top3g[0]
                             + top3w[1] + top3g[1]
                             + top3w[2] + pad;
    }

    return topY + numRows * rowH + S(2);
}

// ── About dialog ──────────────────────────────────────────────────────────────
static void AppendNsbRich(HWND hEdit, const wchar_t* text, bool bold, COLORREF col, int pt, bool center)
{
    static wchar_t s_face[LF_FACESIZE] = {};
    static int     s_twips = 0;
    if (!s_twips) {
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        wcscpy_s(s_face, ncm.lfMessageFont.lfFaceName);
        HDC hdc = GetDC(hEdit);
        int dpiY = GetDeviceCaps(hdc, LOGPIXELSY); ReleaseDC(hEdit, hdc);
        if (dpiY <= 0) dpiY = 96;
        s_twips = MulDiv((int)(abs(ncm.lfMessageFont.lfHeight) * 1.2f + 0.5f), 1440, dpiY);
        if (s_twips <= 0) s_twips = 180;
    }
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask     = CFM_COLOR | CFM_BOLD | CFM_SIZE | CFM_FACE;
    cf.crTextColor = col;
    cf.dwEffects  = bold ? CFE_BOLD : 0;
    cf.yHeight    = (pt > 0) ? pt * 20 : s_twips;
    wcscpy_s(cf.szFaceName, s_face);
    GETTEXTLENGTHEX gtl = { GTL_DEFAULT, 1200 };
    LONG len = (LONG)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    if (center) {
        PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_ALIGNMENT; pf.wAlignment = PFA_CENTER;
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
    if (center) {
        PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_ALIGNMENT; pf.wAlignment = PFA_LEFT;
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
}

struct NsbAboutState {
    Gdiplus::Image* logo;
    WNDPROC         origEdit;
};
static NsbAboutState s_nas = {};

static LRESULT CALLBACK NsbAboutEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT && s_nas.logo) {
        CallWindowProcW(s_nas.origEdit, hwnd, msg, wParam, lParam);
        HDC hdc = GetDC(hwnd);
        Gdiplus::Graphics g(hdc);
        POINT pt = {}; SendMessageW(hwnd, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
        RECT rc; GetClientRect(hwnd, &rc);
        int lw = (int)(s_nas.logo->GetWidth()  * 0.75f);
        int lh = (int)(s_nas.logo->GetHeight() * 0.75f);
        int x  = (rc.right - lw) / 2;
        int y  = 10 - pt.y;
        if (y + lh > 0 && y < rc.bottom)
            g.DrawImage(s_nas.logo, x, y, lw, lh);
        ReleaseDC(hwnd, hdc);
        return 0;
    }
    return CallWindowProcW(s_nas.origEdit, hwnd, msg, wParam, lParam);
}

static void ShowNsbLicenseDialog(HWND parent); // forward declaration
// ── FTP dialog forward declarations ──────────────────────────────────────────
static void Ne_ShowFtpSiteDialog(HWND parent, NeProfile* existing);
static void Ne_ShowFtpBrowser(HWND parent, int64_t profileId);
static void Ne_ShowChmodDialog(HWND parent, const std::wstring& remotePath, int currentMode);
static bool Ne_ShowPasswordPrompt(HWND parent, const std::wstring& siteName, std::wstring& outPw);
static void Ne_UpdateFtpStatus(HWND hwnd);

// Rebuild the FTP popup from scratch (called in WM_INITMENUPOPUP for s_hFtpMenu).
static void Ne_RebuildFtpMenu(HWND hwnd)
{
    while (GetMenuItemCount(s_hFtpMenu) > 0)
        RemoveMenu(s_hFtpMenu, 0, MF_BYPOSITION);
    Ne_AppendMenuOD(s_hFtpMenu, MF_STRING, IDM_FTP_ADD_SITE, Ls(L"FTP_ADD_SITE"));
    NeTabDoc* aDoc = NeTabs_GetActiveDoc(hwnd);
    int64_t activeTabProfileId = (aDoc && aDoc->isFtpFile) ? aDoc->ftpProfileId : 0;
    if (activeTabProfileId && NeFtp_IsConnected(activeTabProfileId))
        Ne_AppendMenuOD(s_hFtpMenu, MF_STRING, IDM_FTP_DISCONNECT, Ls(L"FTP_DISCONNECT"));
    Ne_AppendMenuOD(s_hFtpMenu, MF_SEPARATOR, 0, NULL);
    std::vector<NeProfile> ftpProfiles;
    NeProfiles_List(ftpProfiles);
    for (int pi = 0; pi < (int)ftpProfiles.size() && pi < 100; ++pi) {
        std::wstring label = ftpProfiles[pi].friendlyName
                           + L"  (" + ftpProfiles[pi].protocol + L")";
        HICON hIcon = (activeTabProfileId && ftpProfiles[pi].id == activeTabProfileId)
                      ? g_hFtpMenuIcon : NULL;
        Ne_AppendMenuOD(s_hFtpMenu, MF_STRING,
                        IDM_FTP_CONNECT_BASE + pi, label.c_str(),
                        false, hIcon);
    }
}

static void ShowNsbCreditsDialog(HWND parent); // forward declaration

static LRESULT CALLBACK NsbAboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) {
            ShowNsbLicenseDialog(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == 1002) {
            ShowNsbCreditsDialog(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_DRAWITEM: {
        NeDialogData* dd = (NeDialogData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (dd) Ne_DrawDialogButton((const DRAWITEMSTRUCT*)lParam, dd);
        return TRUE;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, RGB(255,255,255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowNsbLicenseDialog(HWND parent)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {}; wc.lpfnWndProc = NsbAboutWndProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = L"NsbLicClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) RegisterClassW(&wc);

    const int W = S(620), H = S(580);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NsbLicClass", L"GNU General Public License v2",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    // GNU logo BMP
    wchar_t exeDir[MAX_PATH]; GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    wchar_t* sl = wcsrchr(exeDir, L'\\'); if (sl) *(sl+1) = 0;
    wchar_t bmpPath[MAX_PATH]; wcscpy_s(bmpPath, exeDir); wcscat_s(bmpPath, L"GnuLogo.bmp");
    HBITMAP hBmp = (HBITMAP)LoadImageW(NULL, bmpPath, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    int logoH = 0;
    if (hBmp) {
        BITMAP bm; GetObject(hBmp, sizeof(bm), &bm);
        logoH = bm.bmHeight + S(10);
        RECT rcC0; GetClientRect(dlg, &rcC0);
        HWND hLogo = CreateWindowExW(0, L"STATIC", NULL,
            WS_CHILD|WS_VISIBLE|SS_BITMAP|SS_CENTERIMAGE,
            (rcC0.right - bm.bmWidth)/2, S(10), bm.bmWidth, bm.bmHeight,
            dlg, (HMENU)101, hi, NULL);
        SendMessageW(hLogo, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp);
    }

    LoadLibraryW(L"Msftedit.dll");
    RECT rcC; GetClientRect(dlg, &rcC);
    const int PAD = S(10), BTN_H = S(30);
    int editTop = logoH > 0 ? logoH + S(20) : PAD;
    int editH   = rcC.bottom - editTop - PAD - BTN_H - PAD;

    HWND hEdit = CreateWindowExW(0, L"RICHEDIT50W", NULL,
        WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,
        PAD, editTop, rcC.right-2*PAD, editH, dlg, (HMENU)200, hi, NULL);
    if (!hEdit) { DestroyWindow(dlg); return; }
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0);

    // Title header
    AppendNsbRich(hEdit, L"GNU GENERAL PUBLIC LICENSE\r\n", true,  RGB(0,70,140), 14, true);
    AppendNsbRich(hEdit, L"Version 2, June 1991\r\n\r\n",   false, RGB(0,70,140), 10, true);

    // Load and parse GPLv2.md
    wchar_t mdPath[MAX_PATH]; wcscpy_s(mdPath, exeDir); wcscat_s(mdPath, L"GPLv2.md");
    HANDLE hf = CreateFileW(mdPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hf, NULL);
        if (sz && sz < 1024*1024) {
            std::string buf(sz, '\0'); DWORD rd;
            ReadFile(hf, buf.data(), sz, &rd, NULL);
            int wn = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)rd, NULL, 0);
            std::wstring text(wn, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)rd, text.data(), wn);
            std::wstringstream ss(text);
            std::wstring line;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (line.find(L"TERMS AND CONDITIONS") != std::wstring::npos ||
                    line.find(L"NO WARRANTY")          != std::wstring::npos) {
                    AppendNsbRich(hEdit, (L"\r\n" + line + L"\r\n").c_str(), true, RGB(139,0,0), 11, false);
                } else if (line == L"Preamble") {
                    AppendNsbRich(hEdit, (L"\r\n" + line + L"\r\n").c_str(), true, RGB(0,70,140), 12, false);
                } else if (!line.empty() && line.length() < 150 &&
                           (line[0] >= L'0' && line[0] <= L'9') && line.find(L'.') < 3) {
                    AppendNsbRich(hEdit, (L"\r\n" + line + L"\r\n").c_str(), true, RGB(0,0,139), 10, false);
                } else if (line.length() > 1 &&
                           (line[0]==L'a'||line[0]==L'b'||line[0]==L'c') && line[1]==L')') {
                    AppendNsbRich(hEdit, (line + L"\r\n").c_str(), true, RGB(0,0,0), 0, false);
                } else {
                    AppendNsbRich(hEdit, (line + L"\r\n").c_str(), false, RGB(40,40,40), 0, false);
                }
            }
        }
        CloseHandle(hf);
    } else {
        AppendNsbRich(hEdit, L"GPLv2.md not found.\r\n", true, RGB(139,0,0), 0, false);
    }

    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);

    NeDialogData licDD = {};
    licDD.buttonCount = 1;
    licDD.buttons[0] = { IDOK, Ls(L"BTN_CLOSE"), NeBtnTone::Red, IDI_ERROR,
                         Ne_MeasureButtonWidth(Ls(L"BTN_CLOSE")) };
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&licDD);

    int bw = licDD.buttons[0].width;
    int bx = (rcC.right - bw) / 2, by = rcC.bottom - PAD - BTN_H;
    HWND hBtn = CreateWindowExW(0, L"BUTTON", licDD.buttons[0].text.c_str(),
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
        bx, by, bw, BTN_H, dlg, (HMENU)IDOK, hi, NULL);
    {
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
    }

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(hBtn);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (hBmp) DeleteObject(hBmp);
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
}

// ============================================================================
//  FTP / SFTP dialogs
// ============================================================================

// ── Ne_UpdateFtpStatus ───────────────────────────────────────────────────────
static void Ne_UpdateFtpStatus(HWND hwnd)
{
    HWND hSb = GetDlgItem(hwnd, IDC_NE_STATUSBAR);
    if (!hSb) return;
    // Show FTP status if the active tab belongs to a connected profile.
    NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
    if (doc && doc->isFtpFile && doc->ftpProfileId > 0
            && NeFtp_IsConnected(doc->ftpProfileId)) {
        NeFtp_SetActiveConn(doc->ftpProfileId);
        const NeProfile& p = NeFtp_GetActiveProfile();
        std::wstring msg = std::wstring(Ls(L"FTP_STATUS"))
                         + L" " + p.friendlyName
                         + L" (" + p.protocol + L")";
        NeStatusBar_SetInfo(hSb, msg.c_str());
    } else {
        // Reset to encoding/language info via the normal update path.
        HWND hEdit = NeTabs_GetActiveEdit(hwnd);
        NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
        if (hEdit && doc) {
            NeEncoding enc = (NeEncoding)doc->encoding;
            if (doc->langId >= 0 && doc->langId < NE_LANG_COUNT)
                NeStatusBar_SetInfo(hSb, s_langs[doc->langId].name);
            else
                NeStatusBar_SetInfo(hSb, Ne_EncLabel(enc));
        } else {
            NeStatusBar_SetInfo(hSb, L"");
        }
    }
}

// ── Password prompt ───────────────────────────────────────────────────────────
struct NePwPromptData {
    std::wstring siteName;
    std::wstring result;
    bool         ok       = false;
    NeDialogData dd;
};

static LRESULT CALLBACK Ne_PwPromptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NePwPromptData* d = (NePwPromptData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            if (!d) break;
            wchar_t buf[512] = {};
            GetWindowTextW(GetDlgItem(hwnd, 101), buf, 512);
            d->result = buf;
            d->ok     = true;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL)
            { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
        return 0;
    case WM_DRAWITEM:
        if (d) Ne_DrawDialogButton((const DRAWITEMSTRUCT*)lParam, &d->dd);
        return TRUE;
    case WM_CLOSE:
        PostQuitMessage(0);
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, RGB(255,255,255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool Ne_ShowPasswordPrompt(HWND parent,
                                   const std::wstring& siteName,
                                   std::wstring& outPw)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {}; wc.lpfnWndProc = Ne_PwPromptProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = L"NeFtpPwClass";
    if (!GetClassInfoW(hi, L"NeFtpPwClass", &wc)) RegisterClassW(&wc);

    const int W = S(340), H = S(160);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;

    NePwPromptData d;
    d.siteName = siteName;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NeFtpPwClass",
        Ls(L"FTP_PW_PROMPT"),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return false;

    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&d);

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    RECT rc; GetClientRect(dlg, &rc);
    const int PAD = S(12), BTN_H = S(28);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // Label
    std::wstring label = std::wstring(Ls(L"FTP_PW_FOR")) + L" " + siteName + L":";
    HWND hLbl = CreateWindowExW(0, L"STATIC", label.c_str(),
        WS_CHILD|WS_VISIBLE|SS_LEFT,
        PAD, PAD, rc.right - 2*PAD, S(20), dlg, (HMENU)102, hi, NULL);
    SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Password edit
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL,
        PAD, PAD + S(26), rc.right - 2*PAD, S(24), dlg, (HMENU)101, hi, NULL);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Buttons
    d.dd.buttonCount = 2;
    d.dd.buttons[0] = { IDOK,     Ls(L"BTN_OK"),     NeBtnTone::Green, IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"BTN_OK")) };
    d.dd.buttons[1] = { IDCANCEL, Ls(L"BTN_CANCEL"), NeBtnTone::Red,   IDI_ERROR,
                         Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL")) };

    int totalBW = d.dd.buttons[0].width + S(8) + d.dd.buttons[1].width;
    int bx = (rc.right - totalBW) / 2, by = rc.bottom - PAD - BTN_H;

    for (int i = 0; i < 2; ++i) {
        HWND hBtn = CreateWindowExW(0, L"BUTTON", d.dd.buttons[i].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            bx, by, d.dd.buttons[i].width, BTN_H,
            dlg, (HMENU)(UINT_PTR)d.dd.buttons[i].id, hi, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
        bx += d.dd.buttons[i].width + S(8);
    }

    SetFocus(hEdit);
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }

    outPw = d.result;
    return d.ok;
}

// ── FTP Site dialog (Add / Edit) ──────────────────────────────────────────────
struct NeFtpSiteData {
    NeProfile    profile;
    bool         isEdit    = false;
    bool         saved     = false;
    bool         deleted   = false;
    HWND         hChkRemember = NULL;
    NeDialogData dd;
};

static LRESULT CALLBACK Ne_FtpSiteProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NeFtpSiteData* d = (NeFtpSiteData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int ev = HIWORD(wParam);
        // Protocol radio — auto-fill default port
        if (id == 1021 && ev == BN_CLICKED) {
            wchar_t portBuf[16] = {}; GetWindowTextW(GetDlgItem(hwnd, 1024), portBuf, 16);
            if (_wtoi(portBuf) == 22) SetWindowTextW(GetDlgItem(hwnd, 1024), L"21");
        }
        if (id == 1022 && ev == BN_CLICKED) {
            wchar_t portBuf[16] = {}; GetWindowTextW(GetDlgItem(hwnd, 1024), portBuf, 16);
            if (_wtoi(portBuf) == 21) SetWindowTextW(GetDlgItem(hwnd, 1024), L"22");
        }
        // Save
        if (id == 1029) {
            if (!d) break;
            wchar_t buf[512] = {};
            GetWindowTextW(GetDlgItem(hwnd, 1020), buf, 512); d->profile.friendlyName = buf;
            bool isSftp = (SendMessageW(GetDlgItem(hwnd, 1022), BM_GETCHECK, 0, 0) == BST_CHECKED);
            d->profile.protocol = isSftp ? L"SFTP" : L"FTP";
            GetWindowTextW(GetDlgItem(hwnd, 1023), buf, 512); d->profile.host = buf;
            GetWindowTextW(GetDlgItem(hwnd, 1024), buf, 16);  d->profile.port = _wtoi(buf);
            if (d->profile.port <= 0) d->profile.port = isSftp ? 22 : 21;
            GetWindowTextW(GetDlgItem(hwnd, 1025), buf, 512); d->profile.username = buf;
            GetWindowTextW(GetDlgItem(hwnd, 1026), buf, 512); d->profile.password = buf;
            d->profile.rememberPassword =
                (SendMessageW(d->hChkRemember, BM_GETCHECK, 0, 0) == BST_CHECKED);
            GetWindowTextW(GetDlgItem(hwnd, 1028), buf, 512); d->profile.initialPath = buf;
            if (d->profile.friendlyName.empty() || d->profile.host.empty()) break;
            d->saved = true;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        // Delete (edit mode)
        if (id == 1030 && d && d->isEdit) {
            d->deleted = true;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        // Cancel
        if (id == IDCANCEL) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (d) {
            // Let Ne_DrawDialogButton handle all owner-draw buttons (it skips unknown IDs)
            if (Ne_ButtonIndexById(&d->dd, dis->CtlID) >= 0) {
                Ne_DrawDialogButton(dis, &d->dd);
                return TRUE;
            }
        }
        if (d && d->hChkRemember && dis->hwndItem == d->hChkRemember) {
            DrawCustomCheckbox(dis);
            return TRUE;
        }
        return FALSE;
    }
    case WM_SETTINGCHANGE:
        if (d && d->hChkRemember) OnCheckboxSettingChange(hwnd);
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, RGB(255,255,255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ne_ShowFtpSiteDialog(HWND parent, NeProfile* existing)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {}; wc.lpfnWndProc = Ne_FtpSiteProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = L"NeFtpSiteClass";
    if (!GetClassInfoW(hi, L"NeFtpSiteClass", &wc)) RegisterClassW(&wc);

    NeFtpSiteData d;
    d.isEdit = (existing != nullptr);
    if (existing) d.profile = *existing;
    else { d.profile.protocol = L"FTP"; d.profile.port = 21; d.profile.initialPath = L"/"; }

    const int W = S(460), H = S(490);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NeFtpSiteClass",
        d.isEdit ? Ls(L"FTP_DLG_SITE_EDIT") : Ls(L"FTP_DLG_SITE_ADD"),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&d);

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    RECT rc; GetClientRect(dlg, &rc);
    const int PAD = S(16), LBL_W = S(110), EDIT_H = S(26), ROW = S(42), BTN_H = S(34);
    HFONT hFont = Ne_CreateDialogFont(false);  // 12pt Segoe UI
    int editX = PAD + LBL_W + S(6), editW = rc.right - editX - PAD;
    int y0 = PAD;

    // Helper lambdas as inline
    auto AddLabel = [&](const wchar_t* key, int row) {
        HWND h = CreateWindowExW(0, L"STATIC", Ls(key),
            WS_CHILD|WS_VISIBLE|SS_RIGHT,
            PAD, row + S(3), LBL_W, S(18), dlg, NULL, hi, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
    };
    auto AddEdit = [&](int id, const wchar_t* text, int row, DWORD extra = 0) -> HWND {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|extra,
            editX, row, editW, EDIT_H, dlg, (HMENU)(UINT_PTR)id, hi, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        return h;
    };

    // Friendly name
    AddLabel(L"FTP_FRIENDLY_NAME", y0);
    AddEdit(1020, d.profile.friendlyName.c_str(), y0);
    y0 += ROW;

    // Protocol radios
    HWND hLblProto = CreateWindowExW(0, L"STATIC", Ls(L"FTP_PROTOCOL"),
        WS_CHILD|WS_VISIBLE|SS_RIGHT,
        PAD, y0 + S(3), LBL_W, S(18), dlg, NULL, hi, NULL);
    SendMessageW(hLblProto, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hFtp = CreateWindowExW(0, L"BUTTON", L"FTP",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTORADIOBUTTON,
        editX, y0, S(60), S(20), dlg, (HMENU)1021, hi, NULL);
    SendMessageW(hFtp, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hSftp = CreateWindowExW(0, L"BUTTON", L"SFTP",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTORADIOBUTTON,
        editX + S(70), y0, S(60), S(20), dlg, (HMENU)1022, hi, NULL);
    SendMessageW(hSftp, WM_SETFONT, (WPARAM)hFont, TRUE);

    SendMessageW(d.profile.protocol == L"SFTP" ? hSftp : hFtp, BM_SETCHECK, BST_CHECKED, 0);
    y0 += ROW;

    // Host
    AddLabel(L"FTP_HOST", y0);
    AddEdit(1023, d.profile.host.c_str(), y0);
    y0 += ROW;

    // Port
    AddLabel(L"FTP_PORT", y0);
    wchar_t portStr[16] = {}; swprintf_s(portStr, L"%d", d.profile.port);
    AddEdit(1024, portStr, y0, ES_NUMBER);
    y0 += ROW;

    // Username
    AddLabel(L"FTP_USERNAME", y0);
    AddEdit(1025, d.profile.username.c_str(), y0);
    y0 += ROW;

    // Password
    AddLabel(L"FTP_PASSWORD", y0);
    AddEdit(1026, d.profile.password.c_str(), y0, ES_PASSWORD);
    y0 += ROW;

    // Remember password checkbox
    d.hChkRemember = CreateCustomCheckbox(dlg, 1027,
        Ls(L"FTP_REMEMBER_PWD"), d.profile.rememberPassword,
        PAD, y0, rc.right - 2*PAD, S(24), hi);
    SendMessageW(d.hChkRemember, WM_SETFONT, (WPARAM)hFont, TRUE);
    y0 += S(36);

    // Initial folder
    AddLabel(L"FTP_INITIAL_PATH", y0);
    AddEdit(1028, d.profile.initialPath.c_str(), y0);
    y0 += ROW;

    // Buttons
    int numBtns = d.isEdit ? 3 : 2;
    d.dd.buttonCount = numBtns;
    d.dd.buttons[0] = { 1029, Ls(L"FTP_SAVE"),   NeBtnTone::Green, IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"FTP_SAVE")) };
    d.dd.buttons[1] = { IDCANCEL, Ls(L"BTN_CANCEL"), NeBtnTone::Blue, IDI_ERROR,
                         Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL")) };
    if (d.isEdit)
        d.dd.buttons[2] = { 1030, Ls(L"FTP_DELETE"), NeBtnTone::Red, IDI_ERROR,
                             Ne_MeasureButtonWidth(Ls(L"FTP_DELETE")) };

    int totalBW = 0;
    for (int i = 0; i < numBtns; ++i) totalBW += d.dd.buttons[i].width + (i ? S(8) : 0);
    int bx = (rc.right - totalBW) / 2, by = rc.bottom - PAD - BTN_H;
    for (int i = 0; i < numBtns; ++i) {
        HWND hBtn = CreateWindowExW(0, L"BUTTON", d.dd.buttons[i].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            bx, by, d.dd.buttons[i].width, BTN_H,
            dlg, (HMENU)(UINT_PTR)d.dd.buttons[i].id, hi, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
        bx += d.dd.buttons[i].width + S(8);
    }

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(GetDlgItem(dlg, 1020));
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    DeleteObject(hFont);

    if (d.deleted && d.profile.id > 0) {
        NeProfiles_Delete(d.profile.id);
    } else if (d.saved) {
        if (d.isEdit)
            NeProfiles_Update(d.profile);
        else
            NeProfiles_Add(d.profile);
    }
}

// ── chmod dialog ──────────────────────────────────────────────────────────────
struct NeChmodData {
    std::wstring remotePath;
    int          mode     = 0644;
    bool         applied  = false;
    HWND         hChk[9]  = {};      // [0-2]=owner rwx, [3-5]=group rwx, [6-8]=other rwx
    HWND         hOctal   = NULL;
    NeDialogData dd;
};

static void Ne_ChmodUpdateOctal(NeChmodData* d)
{
    if (!d->hOctal) return;
    int m = 0;
    static const int bits[9] = { 0400,0200,0100, 0040,0020,0010, 0004,0002,0001 };
    for (int i = 0; i < 9; ++i) {
        if (d->hChk[i] && SendMessageW(d->hChk[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
            m |= bits[i];
    }
    d->mode = m;
    wchar_t buf[16];
    swprintf_s(buf, L"%s %03o", Ls(L"FTP_CHMOD_OCTAL"), m);
    SetWindowTextW(d->hOctal, buf);
}

static LRESULT CALLBACK Ne_ChmodProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NeChmodData* d = (NeChmodData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int ev = HIWORD(wParam);
        // Any checkbox click → update octal display
        if (ev == BN_CLICKED && id >= 3000 && id < 3009)
            Ne_ChmodUpdateOctal(d);
        // Apply
        if (id == 3010) {
            if (!d) break;
            Ne_ChmodUpdateOctal(d);
            if (NeFtp_Chmod(d->remotePath, d->mode)) {
                d->applied = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            } else {
                MessageBoxW(hwnd, NeFtp_GetLastError().c_str(), Ls(L"FTP_DLG_CHMOD"), MB_ICONERROR|MB_OK);
            }
        }
        if (id == IDCANCEL) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!d) return FALSE;
        if (Ne_ButtonIndexById(&d->dd, dis->CtlID) >= 0) {
            Ne_DrawDialogButton(dis, &d->dd);
            return TRUE;
        }
        for (int i = 0; i < 9; ++i) {
            if (d->hChk[i] && dis->hwndItem == d->hChk[i]) {
                DrawCustomCheckbox(dis);
                return TRUE;
            }
        }
        return FALSE;
    }
    case WM_SETTINGCHANGE:
        if (d) OnCheckboxSettingChange(hwnd);
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, RGB(255,255,255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ne_ShowChmodDialog(HWND parent, const std::wstring& remotePath, int currentMode)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {}; wc.lpfnWndProc = Ne_ChmodProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = L"NeFtpChmodClass";
    if (!GetClassInfoW(hi, L"NeFtpChmodClass", &wc)) RegisterClassW(&wc);

    NeChmodData d;
    d.remotePath = remotePath;
    d.mode = (currentMode >= 0) ? currentMode : 0644;

    const int W = S(440), H = S(360);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;
    if (y < 30) y = 30;

    // Build window title with the filename only
    std::wstring title = std::wstring(Ls(L"FTP_DLG_CHMOD")) + L" \u2014 ";
    size_t sl = remotePath.rfind(L'/');
    title += (sl != std::wstring::npos) ? remotePath.substr(sl + 1) : remotePath;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NeFtpChmodClass", title.c_str(),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&d);

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    RECT rc; GetClientRect(dlg, &rc);
    const int PAD = S(18), BTN_H = S(34), CHK_SZ = S(24);
    HFONT hFont = Ne_CreateDialogFont(false);  // 12pt Segoe UI

    // Grid geometry
    const wchar_t* colNames[3] = { Ls(L"FTP_CHMOD_OWNER"), Ls(L"FTP_CHMOD_GROUP"), Ls(L"FTP_CHMOD_OTHER") };
    const wchar_t* rowNames[3] = { Ls(L"FTP_CHMOD_READ"),  Ls(L"FTP_CHMOD_WRITE"), Ls(L"FTP_CHMOD_EXEC") };
    int colW    = S(96);
    int rowLblW = S(30);
    int rowH    = S(44);
    int gridX   = (rc.right - (rowLblW + 3 * colW)) / 2;
    int gridY   = PAD + S(34);

    // Column header labels
    for (int c = 0; c < 3; ++c) {
        HWND h = CreateWindowExW(0, L"STATIC", colNames[c],
            WS_CHILD|WS_VISIBLE|SS_CENTER,
            gridX + rowLblW + c * colW, PAD, colW, S(26), dlg, NULL, hi, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    // Row labels and custom checkboxes
    const int bits9[9] = { 0400,0200,0100, 0040,0020,0010, 0004,0002,0001 };
    for (int r = 0; r < 3; ++r) {
        HWND hLbl = CreateWindowExW(0, L"STATIC", rowNames[r],
            WS_CHILD|WS_VISIBLE|SS_CENTER,
            gridX, gridY + r * rowH + (rowH - S(26)) / 2, rowLblW, S(26), dlg, NULL, hi, NULL);
        SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

        for (int c = 0; c < 3; ++c) {
            int bitIdx = c * 3 + r;
            bool chk = (d.mode & bits9[bitIdx]) != 0;
            int cx = gridX + rowLblW + c * colW + (colW - CHK_SZ) / 2;
            int cy = gridY + r * rowH + (rowH - CHK_SZ) / 2;
            d.hChk[bitIdx] = CreateCustomCheckbox(dlg, 3000 + bitIdx,
                L"", chk, cx, cy, CHK_SZ, CHK_SZ, hi);
            SendMessageW(d.hChk[bitIdx], WM_SETFONT, (WPARAM)hFont, TRUE);
        }
    }

    // Octal display
    wchar_t octalBuf[32] = {};
    swprintf_s(octalBuf, L"%s %03o", Ls(L"FTP_CHMOD_OCTAL"), d.mode);
    d.hOctal = CreateWindowExW(0, L"STATIC", octalBuf,
        WS_CHILD|WS_VISIBLE|SS_CENTER,
        PAD, gridY + 3 * rowH + S(10), rc.right - 2*PAD, S(26), dlg, NULL, hi, NULL);
    SendMessageW(d.hOctal, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Buttons: Apply (Green) + Cancel (Blue)
    d.dd.buttonCount = 2;
    d.dd.buttons[0] = { 3010,     Ls(L"FTP_CHMOD_APPLY"), NeBtnTone::Green, IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"FTP_CHMOD_APPLY")) };
    d.dd.buttons[1] = { IDCANCEL, Ls(L"BTN_CANCEL"),      NeBtnTone::Blue,  IDI_ERROR,
                         Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL")) };

    int totalBW = d.dd.buttons[0].width + S(10) + d.dd.buttons[1].width;
    int bx = (rc.right - totalBW) / 2, by = rc.bottom - PAD - BTN_H;
    for (int i = 0; i < 2; ++i) {
        HWND hBtn = CreateWindowExW(0, L"BUTTON", d.dd.buttons[i].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            bx, by, d.dd.buttons[i].width, BTN_H,
            dlg, (HMENU)(UINT_PTR)d.dd.buttons[i].id, hi, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
        bx += d.dd.buttons[i].width + S(10);
    }

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    DeleteObject(hFont);
}

// ── Simple text-input dialog ──────────────────────────────────────────────────
struct NeInputDlgData {
    const wchar_t* prompt = nullptr;
    std::wstring   value;
    bool           ok     = false;
    NeDialogData   dd;
};

static LRESULT CALLBACK Ne_InputDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NeInputDlgData* d = (NeInputDlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) {
            if (d) {
                wchar_t buf[MAX_PATH] = {};
                GetDlgItemTextW(hwnd, 4001, buf, MAX_PATH);
                d->value = buf;
                d->ok = (buf[0] != L'\0');
            }
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        if (id == IDCANCEL) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (d && Ne_ButtonIndexById(&d->dd, dis->CtlID) >= 0) {
            Ne_DrawDialogButton(dis, &d->dd);
            return TRUE;
        }
        return FALSE;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, RGB(255,255,255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool Ne_ShowInputDialog(HWND parent, const wchar_t* title, const wchar_t* prompt, std::wstring& out)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {}; wc.lpfnWndProc = Ne_InputDlgProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = L"NeInputDlgClass";
    if (!GetClassInfoW(hi, L"NeInputDlgClass", &wc)) RegisterClassW(&wc);

    NeInputDlgData d;
    d.prompt = prompt;

    const int W = S(360), H = S(150);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NeInputDlgClass", title, WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return false;

    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&d);

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    RECT rc; GetClientRect(dlg, &rc);
    const int PAD = S(14), BTN_H = S(32);
    HFONT hFont = Ne_CreateDialogFont(false);

    HWND hLbl = CreateWindowExW(0, L"STATIC", prompt, WS_CHILD|WS_VISIBLE|SS_LEFT,
        PAD, PAD, rc.right - 2*PAD, S(22), dlg, NULL, hi, NULL);
    SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
        PAD, PAD + S(28), rc.right - 2*PAD, S(26), dlg, (HMENU)4001, hi, NULL);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    d.dd.buttonCount = 2;
    d.dd.buttons[0] = { IDOK,     Ls(L"BTN_OK"),     NeBtnTone::Green, IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"BTN_OK")) };
    d.dd.buttons[1] = { IDCANCEL, Ls(L"BTN_CANCEL"), NeBtnTone::Blue,  IDI_ERROR,
                         Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL")) };

    int totalBW = d.dd.buttons[0].width + S(8) + d.dd.buttons[1].width;
    int bx = (rc.right - totalBW) / 2, by = rc.bottom - PAD - BTN_H;
    for (int i = 0; i < 2; ++i) {
        HWND hBtn = CreateWindowExW(0, L"BUTTON", d.dd.buttons[i].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            bx, by, d.dd.buttons[i].width, BTN_H,
            dlg, (HMENU)(UINT_PTR)d.dd.buttons[i].id, hi, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
        bx += d.dd.buttons[i].width + S(8);
    }

    SetFocus(hEdit);
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    DeleteObject(hFont);
    if (d.ok) { out = d.value; return true; }
    return false;
}

// ── FTP Browser (TreeView) ────────────────────────────────────────────────────
struct NeFtpTreeNode {
    std::wstring fullPath;
    std::wstring name;
    bool         isDir  = false;
    bool         loaded = false;
};

struct NeFtpBrowserData {
    HWND                       hwndParent;
    HWND                       hwndTree;
    std::vector<NeFtpTreeNode> nodes;
    int                        iFolderClosed = 0;
    int                        iFolderOpen   = 0;
    HTREEITEM                  htiRoot       = nullptr;
    std::wstring               pendingOpenLocal;
    std::wstring               pendingOpenRemote;
    NeDialogData               dd;  // holds Refresh + Close
    int64_t                    profileId     = 0;
    // ── Save mode ─────────────────────────────────────────────────────────────
    bool                       saveMode           = false;
    HWND                       hFilenameEdit      = NULL;  // filename input (save mode)
    std::wstring               pendingSaveRemotePath;      // set when user confirms save
};

static HTREEITEM Ne_FtpTreeAddItem(HWND hTree, HTREEITEM hParent,
    const std::wstring& name, bool isDir, int nodeIdx,
    int iFolderClosed, int iFolderOpen)
{
    SHFILEINFOW sfi = {};
    int iImg = iFolderClosed;
    if (!isDir) {
        SHGetFileInfoW(name.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
            SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
        iImg = sfi.iIcon;
    }
    TVINSERTSTRUCTW tvi = {};
    tvi.hParent             = hParent;
    tvi.hInsertAfter        = TVI_LAST;
    tvi.item.mask           = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tvi.item.pszText        = (LPWSTR)name.c_str();
    tvi.item.lParam         = (LPARAM)(INT_PTR)nodeIdx;
    tvi.item.iImage         = iImg;
    tvi.item.iSelectedImage = isDir ? iFolderOpen : iImg;
    HTREEITEM hItem = TreeView_InsertItem(hTree, &tvi);
    if (isDir && hItem) {
        TVINSERTSTRUCTW dummy = {};
        dummy.hParent      = hItem;
        dummy.hInsertAfter = TVI_LAST;
        dummy.item.mask    = TVIF_TEXT | TVIF_PARAM;
        dummy.item.pszText = (LPWSTR)L"";
        dummy.item.lParam  = (LPARAM)(INT_PTR)-1;
        TreeView_InsertItem(hTree, &dummy);
    }
    return hItem;
}

static void Ne_FtpTreeLoadChildren(HWND hwnd, HTREEITEM hItem, NeFtpBrowserData* d, int nodeIdx)
{
    if (d->nodes[nodeIdx].loaded) return;
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
    std::vector<NeFtpEntry> entries;
    bool ok = NeFtp_ListDir(d->nodes[nodeIdx].fullPath, entries);
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
    if (!ok) {
        MessageBoxW(hwnd, NeFtp_GetLastError().c_str(), Ls(L"FTP_DLG_BROWSER"), MB_ICONERROR|MB_OK);
        return;
    }
    // Remove dummy child
    HTREEITEM hDummy = TreeView_GetChild(d->hwndTree, hItem);
    if (hDummy) TreeView_DeleteItem(d->hwndTree, hDummy);
    // Sort: dirs first, then alphabetically
    std::sort(entries.begin(), entries.end(), [](const NeFtpEntry& a, const NeFtpEntry& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    std::wstring base = d->nodes[nodeIdx].fullPath;
    if (!base.empty() && base.back() != L'/') base += L'/';
    for (auto& e : entries) {
        NeFtpTreeNode child;
        child.name     = e.name;
        child.fullPath = base + e.name;
        child.isDir    = e.isDir;
        child.loaded   = false;
        int idx = (int)d->nodes.size();
        d->nodes.push_back(child);
        Ne_FtpTreeAddItem(d->hwndTree, hItem, e.name, e.isDir, idx, d->iFolderClosed, d->iFolderOpen);
    }
    d->nodes[nodeIdx].loaded = true;
}

static void Ne_FtpTreeReloadItem(HWND hwnd, NeFtpBrowserData* d, HTREEITEM hItem, int nodeIdx)
{
    d->nodes[nodeIdx].loaded = false;
    HTREEITEM hChild = TreeView_GetChild(d->hwndTree, hItem);
    while (hChild) {
        HTREEITEM hNext = TreeView_GetNextSibling(d->hwndTree, hChild);
        TreeView_DeleteItem(d->hwndTree, hChild);
        hChild = hNext;
    }
    TVINSERTSTRUCTW dummy = {};
    dummy.hParent      = hItem;
    dummy.hInsertAfter = TVI_LAST;
    dummy.item.mask    = TVIF_TEXT | TVIF_PARAM;
    dummy.item.pszText = (LPWSTR)L"";
    dummy.item.lParam  = (LPARAM)(INT_PTR)-1;
    TreeView_InsertItem(d->hwndTree, &dummy);
    TreeView_Expand(d->hwndTree, hItem, TVE_EXPAND);
}

static LRESULT CALLBACK Ne_FtpBrowserProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NeFtpBrowserData* d = (NeFtpBrowserData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDCANCEL || id == IDOK) { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
        if (id == 5001) {
            // Refresh: reload selected dir (or its parent if a file is selected)
            HTREEITEM hSel = TreeView_GetSelection(d->hwndTree);
            HTREEITEM hRefItem = d->htiRoot;
            int refIdx = 0;
            if (hSel) {
                TVITEMW ti = {}; ti.hItem = hSel; ti.mask = TVIF_PARAM;
                TreeView_GetItem(d->hwndTree, &ti);
                int idx = (int)(INT_PTR)ti.lParam;
                if (idx >= 0 && idx < (int)d->nodes.size()) {
                    if (d->nodes[idx].isDir) { hRefItem = hSel; refIdx = idx; }
                    else {
                        HTREEITEM hP = TreeView_GetParent(d->hwndTree, hSel);
                        if (hP) {
                            TVITEMW ti2 = {}; ti2.hItem = hP; ti2.mask = TVIF_PARAM;
                            TreeView_GetItem(d->hwndTree, &ti2);
                            hRefItem = hP;
                            refIdx = (int)(INT_PTR)ti2.lParam;
                        }
                    }
                }
            }
            if (hRefItem) Ne_FtpTreeReloadItem(hwnd, d, hRefItem, refIdx);
            return 0;
        }
        // ── Save mode: "Save here" button ─────────────────────────────────────
        if (id == 5002 && d->saveMode && d->hFilenameEdit) {
            wchar_t fname[MAX_PATH] = {};
            GetWindowTextW(d->hFilenameEdit, fname, MAX_PATH);
            if (fname[0] == L'\0') {
                MessageBoxW(hwnd, Ls(L"FTP_FILENAME_PROMPT"),
                            Ls(L"FTP_SAVE_BROWSER"), MB_ICONWARNING|MB_OK);
                SetFocus(d->hFilenameEdit);
                return 0;
            }
            // Determine current directory from selected tree item
            HTREEITEM hSel = TreeView_GetSelection(d->hwndTree);
            std::wstring dir = L"/";
            if (hSel) {
                TVITEMW ti = {}; ti.hItem = hSel; ti.mask = TVIF_PARAM;
                TreeView_GetItem(d->hwndTree, &ti);
                int idx = (int)(INT_PTR)ti.lParam;
                if (idx >= 0 && idx < (int)d->nodes.size()) {
                    if (d->nodes[idx].isDir) {
                        dir = d->nodes[idx].fullPath;
                    } else {
                        size_t sl = d->nodes[idx].fullPath.rfind(L'/');
                        dir = (sl != std::wstring::npos && sl > 0)
                              ? d->nodes[idx].fullPath.substr(0, sl) : L"/";
                    }
                }
            }
            if (!dir.empty() && dir.back() != L'/') dir += L'/';
            d->pendingSaveRemotePath = dir + fname;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lParam;
        if (!d || hdr->hwndFrom != d->hwndTree) break;

        if (hdr->code == TVN_ITEMEXPANDINGW) {
            NMTREEVIEWW* nm = (NMTREEVIEWW*)lParam;
            if (nm->action == TVE_EXPAND) {
                int idx = (int)(INT_PTR)nm->itemNew.lParam;
                if (idx >= 0 && idx < (int)d->nodes.size() && d->nodes[idx].isDir)
                    Ne_FtpTreeLoadChildren(hwnd, nm->itemNew.hItem, d, idx);
            }
            break;
        }

        if (hdr->code == NM_DBLCLK) {
            HTREEITEM hSel = TreeView_GetSelection(d->hwndTree);
            if (!hSel) break;
            TVITEMW ti = {}; ti.hItem = hSel; ti.mask = TVIF_PARAM;
            TreeView_GetItem(d->hwndTree, &ti);
            int idx = (int)(INT_PTR)ti.lParam;
            if (idx < 0 || idx >= (int)d->nodes.size()) break;
            // ── Save mode: double-click file → pre-fill filename ──────────────
            if (d->saveMode) {
                if (!d->nodes[idx].isDir && d->hFilenameEdit) {
                    SetWindowTextW(d->hFilenameEdit, d->nodes[idx].name.c_str());
                    SetFocus(d->hFilenameEdit);
                }
                break; // don't download
            }
            // ── Open mode: double-click file → download ───────────────────────
            if (d->nodes[idx].isDir) break;
            SetCursor(LoadCursorW(NULL, IDC_WAIT));
            std::wstring localPath;
            bool ok = NeFtp_DownloadToTemp(d->nodes[idx].fullPath, localPath);
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            if (ok) {
                d->pendingOpenLocal  = localPath;
                d->pendingOpenRemote = d->nodes[idx].fullPath;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            } else {
                MessageBoxW(hwnd, NeFtp_GetLastError().c_str(), Ls(L"FTP_DLG_BROWSER"), MB_ICONERROR|MB_OK);
            }
            break;
        }

        if (hdr->code == NM_RCLICK) {
            POINT pt; GetCursorPos(&pt);
            POINT ptC = pt; ScreenToClient(d->hwndTree, &ptC);
            TVHITTESTINFO ht = {}; ht.pt = ptC;
            HTREEITEM hHit = TreeView_HitTest(d->hwndTree, &ht);
            int selIdx = -1;
            if (hHit) {
                TreeView_SelectItem(d->hwndTree, hHit);
                TVITEMW ti = {}; ti.hItem = hHit; ti.mask = TVIF_PARAM;
                TreeView_GetItem(d->hwndTree, &ti);
                selIdx = (int)(INT_PTR)ti.lParam;
            }
            bool hasItem = (selIdx >= 0 && selIdx < (int)d->nodes.size());

            HMENU hCtx = CreatePopupMenu();
            AppendMenuW(hCtx, MF_STRING, 5020, Ls(L"FTP_CTX_NEW_FILE"));
            AppendMenuW(hCtx, MF_STRING, 5021, Ls(L"FTP_CTX_NEW_DIR"));
            if (hasItem) {
                AppendMenuW(hCtx, MF_SEPARATOR, 0, NULL);
                if (!d->nodes[selIdx].isDir && !d->saveMode)
                    AppendMenuW(hCtx, MF_STRING, 5010, Ls(L"FTP_CTX_OPEN"));
                AppendMenuW(hCtx, MF_STRING, 5011, Ls(L"FTP_CTX_CHMOD"));
                if (selIdx > 0)
                    AppendMenuW(hCtx, MF_STRING, 5012, Ls(L"FTP_CTX_DELETE"));
            }

            int cmd = TrackPopupMenuEx(hCtx, TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,
                pt.x, pt.y, hwnd, NULL);
            DestroyMenu(hCtx);
            if (!cmd) break;

            // Determine context directory and its tree item
            std::wstring ctxDir;
            HTREEITEM hCtxDirItem = nullptr;
            int ctxDirIdx = 0;
            if (hasItem) {
                if (d->nodes[selIdx].isDir) {
                    ctxDir = d->nodes[selIdx].fullPath;
                    hCtxDirItem = hHit;
                    ctxDirIdx = selIdx;
                } else {
                    size_t sl = d->nodes[selIdx].fullPath.rfind(L'/');
                    ctxDir = (sl > 0) ? d->nodes[selIdx].fullPath.substr(0, sl) : L"/";
                    hCtxDirItem = TreeView_GetParent(d->hwndTree, hHit);
                    if (hCtxDirItem) {
                        TVITEMW ti2 = {}; ti2.hItem = hCtxDirItem; ti2.mask = TVIF_PARAM;
                        TreeView_GetItem(d->hwndTree, &ti2);
                        ctxDirIdx = (int)(INT_PTR)ti2.lParam;
                    }
                }
            } else {
                ctxDir = L"/";
                hCtxDirItem = d->htiRoot;
                ctxDirIdx = 0;
            }

            if (cmd == 5020) {
                std::wstring fname;
                if (!Ne_ShowInputDialog(hwnd, Ls(L"FTP_CTX_NEW_FILE"), Ls(L"FTP_INPUT_FILENAME"), fname)) break;
                std::wstring newPath = ctxDir;
                if (newPath.back() != L'/') newPath += L'/';
                newPath += fname;
                SetCursor(LoadCursorW(NULL, IDC_WAIT));
                bool ok = NeFtp_CreateEmptyFile(newPath);
                SetCursor(LoadCursorW(NULL, IDC_ARROW));
                if (!ok) { MessageBoxW(hwnd, NeFtp_GetLastError().c_str(), Ls(L"FTP_CTX_NEW_FILE"), MB_ICONERROR|MB_OK); break; }
                if (hCtxDirItem) Ne_FtpTreeReloadItem(hwnd, d, hCtxDirItem, ctxDirIdx);

            } else if (cmd == 5021) {
                std::wstring dname;
                if (!Ne_ShowInputDialog(hwnd, Ls(L"FTP_CTX_NEW_DIR"), Ls(L"FTP_INPUT_DIRNAME"), dname)) break;
                std::wstring newPath = ctxDir;
                if (newPath.back() != L'/') newPath += L'/';
                newPath += dname;
                SetCursor(LoadCursorW(NULL, IDC_WAIT));
                bool ok = NeFtp_MkDir(newPath);
                SetCursor(LoadCursorW(NULL, IDC_ARROW));
                if (!ok) { MessageBoxW(hwnd, NeFtp_GetLastError().c_str(), Ls(L"FTP_CTX_NEW_DIR"), MB_ICONERROR|MB_OK); break; }
                if (hCtxDirItem) Ne_FtpTreeReloadItem(hwnd, d, hCtxDirItem, ctxDirIdx);

            } else if (cmd == 5010 && hasItem && !d->nodes[selIdx].isDir) {
                SetCursor(LoadCursorW(NULL, IDC_WAIT));
                std::wstring localPath;
                bool ok = NeFtp_DownloadToTemp(d->nodes[selIdx].fullPath, localPath);
                SetCursor(LoadCursorW(NULL, IDC_ARROW));
                if (ok) {
                    d->pendingOpenLocal  = localPath;
                    d->pendingOpenRemote = d->nodes[selIdx].fullPath;
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                } else {
                    MessageBoxW(hwnd, NeFtp_GetLastError().c_str(), Ls(L"FTP_DLG_BROWSER"), MB_ICONERROR|MB_OK);
                }

            } else if (cmd == 5011 && hasItem) {
                Ne_ShowChmodDialog(hwnd, d->nodes[selIdx].fullPath, -1);

            } else if (cmd == 5012 && hasItem && selIdx > 0) {
                wchar_t warnBuf[MAX_PATH + 64] = {};
                swprintf_s(warnBuf, Ls(L"MSG_FTP_DELETE_WARN"), d->nodes[selIdx].name.c_str());
                NeDialogButtonSpec btns[2] = {
                    { IDYES,    Ls(L"BTN_OK"),     NeBtnTone::Red,  IDI_WARNING, 0 },
                    { IDCANCEL, Ls(L"BTN_CANCEL"), NeBtnTone::Blue, IDI_ERROR,   0 },
                };
                int r = Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_CTX_DELETE"), warnBuf, btns, 2, IDCANCEL);
                if (r == IDYES) {
                    SetCursor(LoadCursorW(NULL, IDC_WAIT));
                    bool ok = NeFtp_Delete(d->nodes[selIdx].fullPath, d->nodes[selIdx].isDir);
                    SetCursor(LoadCursorW(NULL, IDC_ARROW));
                    if (!ok) {
                        MessageBoxW(hwnd, NeFtp_GetLastError().c_str(), Ls(L"FTP_CTX_DELETE"), MB_ICONERROR|MB_OK);
                    } else if (hCtxDirItem) {
                        Ne_FtpTreeReloadItem(hwnd, d, hCtxDirItem, ctxDirIdx);
                    }
                }
            }
            break;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (d && Ne_ButtonIndexById(&d->dd, dis->CtlID) >= 0) {
            Ne_DrawDialogButton(dis, &d->dd);
            return TRUE;
        }
        return FALSE;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, RGB(255,255,255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ne_ShowFtpBrowser(HWND parent, int64_t profileId)
{
    if (!NeFtp_SetActiveConn(profileId)) return;

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {}; wc.lpfnWndProc = Ne_FtpBrowserProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = L"NeFtpBrowserClass";
    if (!GetClassInfoW(hi, L"NeFtpBrowserClass", &wc)) RegisterClassW(&wc);

    const int W = S(480), H = S(580);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;
    if (y < 30) y = 30;

    NeFtpBrowserData d = {};
    d.hwndParent = parent;
    d.profileId  = profileId;

    std::wstring title = std::wstring(Ls(L"FTP_DLG_BROWSER"))
                       + L" \u2014 " + NeFtp_GetActiveProfile().friendlyName;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NeFtpBrowserClass", title.c_str(),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&d);

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    RECT rc; GetClientRect(dlg, &rc);
    const int PAD = S(10), BTN_H = S(32);
    HFONT hFont = Ne_CreateDialogFont(false);

    // System imagelist for file-type icons
    SHFILEINFOW sfi = {};
    HIMAGELIST hSysImgList = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi),
        SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    SHGetFileInfoW(L"x", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
        SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    d.iFolderClosed = sfi.iIcon;
    SHGetFileInfoW(L"x", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
        SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_OPENICON);
    d.iFolderOpen = sfi.iIcon;

    // Toolbar + Close buttons stored in dd for owner-draw
    d.dd.buttonCount = 2;
    d.dd.buttons[0] = { 5001, Ls(L"BTN_RELOAD"), NeBtnTone::Blue, IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"BTN_RELOAD")) };
    d.dd.buttons[1] = { IDOK, Ls(L"BTN_CLOSE"),  NeBtnTone::Red,  IDI_ERROR,
                         Ne_MeasureButtonWidth(Ls(L"BTN_CLOSE")) };

    // Refresh button (top-right)
    {
        int bw = d.dd.buttons[0].width;
        HWND hBtn = CreateWindowExW(0, L"BUTTON", d.dd.buttons[0].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            rc.right - PAD - bw, PAD, bw, BTN_H,
            dlg, (HMENU)(UINT_PTR)5001, hi, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
    }

    // TreeView
    int treeY = PAD + BTN_H + S(6);
    int treeH = rc.bottom - treeY - PAD - BTN_H - S(8);
    d.hwndTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|
        TVS_HASLINES|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS,
        PAD, treeY, rc.right - 2*PAD, treeH, dlg, (HMENU)5005, hi, NULL);
    if (hSysImgList)
        TreeView_SetImageList(d.hwndTree, hSysImgList, TVSIL_NORMAL);
    SendMessageW(d.hwndTree, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Close button (bottom-centre)
    {
        int bw = d.dd.buttons[1].width;
        HWND hBtn = CreateWindowExW(0, L"BUTTON", d.dd.buttons[1].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            (rc.right - bw) / 2, rc.bottom - PAD - BTN_H, bw, BTN_H,
            dlg, (HMENU)IDOK, hi, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
    }

    // Seed root node
    {
        std::wstring rootPath = NeFtp_GetActiveProfile().initialPath;
        if (rootPath.empty()) rootPath = L"/";
        NeFtpTreeNode rootNode;
        rootNode.name     = rootPath;
        rootNode.fullPath = rootPath;
        rootNode.isDir    = true;
        rootNode.loaded   = false;
        d.nodes.push_back(rootNode);
        d.htiRoot = Ne_FtpTreeAddItem(d.hwndTree, TVI_ROOT, rootPath, true, 0,
                                       d.iFolderClosed, d.iFolderOpen);
        // Expand root immediately
        TreeView_Expand(d.hwndTree, d.htiRoot, TVE_EXPAND);
    }

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    DeleteObject(hFont);

    if (!d.pendingOpenLocal.empty()) {
        NeTabs_AddUntitled(parent);
        Ne_LoadPathIntoEditor(parent, d.pendingOpenLocal);
        NeTabDoc* doc = NeTabs_GetActiveDoc(parent);
        if (doc) {
            doc->isFtpFile        = true;
            doc->ftpProfileId     = d.profileId;
            doc->ftpRemotePath    = d.pendingOpenRemote;
            doc->ftpFriendlyName  = NeFtp_GetActiveProfile().friendlyName;
        }
    }
}

// Opens the FTP browser in "save" mode. Returns the chosen remote path, or
// empty string if the user cancelled. Does NOT close the connection.
static std::wstring Ne_ShowFtpBrowserSave(HWND parent, int64_t profileId,
                                          const std::wstring& suggestedName)
{
    if (!NeFtp_SetActiveConn(profileId)) return L"";

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {}; wc.lpfnWndProc = Ne_FtpBrowserProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = L"NeFtpBrowserClass";
    if (!GetClassInfoW(hi, L"NeFtpBrowserClass", &wc)) RegisterClassW(&wc);

    const int W = S(480), H = S(620);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;
    if (y < 30) y = 30;

    NeFtpBrowserData d = {};
    d.hwndParent = parent;
    d.profileId  = profileId;
    d.saveMode   = true;

    std::wstring title = std::wstring(Ls(L"FTP_SAVE_BROWSER"))
                       + L" \u2014 " + NeFtp_GetActiveProfile().friendlyName;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NeFtpBrowserClass", title.c_str(),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return L"";

    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&d);

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    RECT rc; GetClientRect(dlg, &rc);
    const int PAD = S(10), BTN_H = S(32), EDIT_H = S(26), LABEL_H = S(18);
    HFONT hFont = Ne_CreateDialogFont(false);

    // System imagelist for file-type icons
    SHFILEINFOW sfi = {};
    HIMAGELIST hSysImgList = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi),
        SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    SHGetFileInfoW(L"x", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
        SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    d.iFolderClosed = sfi.iIcon;
    SHGetFileInfoW(L"x", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
        SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_OPENICON);
    d.iFolderOpen = sfi.iIcon;

    // Buttons: Refresh (top-right) + Save here (bottom) + Cancel (bottom)
    d.dd.buttonCount = 3;
    d.dd.buttons[0] = { 5001, Ls(L"BTN_RELOAD"),      NeBtnTone::Blue, IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"BTN_RELOAD")) };
    d.dd.buttons[1] = { 5002, Ls(L"FTP_SAVE_HERE"),   NeBtnTone::Green, IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"FTP_SAVE_HERE")) };
    d.dd.buttons[2] = { IDOK, Ls(L"BTN_CANCEL"),      NeBtnTone::Red,  IDI_ERROR,
                         Ne_MeasureButtonWidth(Ls(L"BTN_CANCEL")) };

    // Refresh button (top-right)
    {
        int bw = d.dd.buttons[0].width;
        HWND hBtn = CreateWindowExW(0, L"BUTTON", d.dd.buttons[0].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            rc.right - PAD - bw, PAD, bw, BTN_H,
            dlg, (HMENU)(UINT_PTR)5001, hi, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
    }

    // Filename row: label + edit box (above bottom buttons)
    int fnRowY = rc.bottom - PAD - BTN_H - S(6) - EDIT_H - S(4) - LABEL_H;
    {
        HWND hLbl = CreateWindowExW(0, L"STATIC", Ls(L"FTP_FILENAME_PROMPT"),
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            PAD, fnRowY, rc.right - 2*PAD, LABEL_H,
            dlg, NULL, hi, NULL);
        SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

        d.hFilenameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", suggestedName.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            PAD, fnRowY + LABEL_H + S(2), rc.right - 2*PAD, EDIT_H,
            dlg, (HMENU)(UINT_PTR)5003, hi, NULL);
        SendMessageW(d.hFilenameEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    // TreeView (fills between top buttons and filename row)
    int treeY = PAD + BTN_H + S(6);
    int treeH = fnRowY - treeY - S(4);
    d.hwndTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|
        TVS_HASLINES|TVS_HASBUTTONS|TVS_LINESATROOT|TVS_SHOWSELALWAYS,
        PAD, treeY, rc.right - 2*PAD, treeH, dlg, (HMENU)5005, hi, NULL);
    if (hSysImgList)
        TreeView_SetImageList(d.hwndTree, hSysImgList, TVSIL_NORMAL);
    SendMessageW(d.hwndTree, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Bottom buttons: "Save here" and "Cancel" side by side
    {
        int bSave   = d.dd.buttons[1].width;
        int bCancel = d.dd.buttons[2].width;
        int totalW  = bSave + PAD + bCancel;
        int btnY    = rc.bottom - PAD - BTN_H;
        int startX  = (rc.right - totalW) / 2;

        HWND hSave = CreateWindowExW(0, L"BUTTON", d.dd.buttons[1].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            startX, btnY, bSave, BTN_H,
            dlg, (HMENU)(UINT_PTR)5002, hi, NULL);
        SendMessageW(hSave, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC p1 = (WNDPROC)SetWindowLongPtrW(hSave, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hSave, L"NePrevProc", (HANDLE)p1);

        HWND hCancel = CreateWindowExW(0, L"BUTTON", d.dd.buttons[2].text.c_str(),
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            startX + bSave + PAD, btnY, bCancel, BTN_H,
            dlg, (HMENU)IDOK, hi, NULL);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
        WNDPROC p2 = (WNDPROC)SetWindowLongPtrW(hCancel, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hCancel, L"NePrevProc", (HANDLE)p2);
    }

    // Seed root node
    {
        std::wstring rootPath = NeFtp_GetActiveProfile().initialPath;
        if (rootPath.empty()) rootPath = L"/";
        NeFtpTreeNode rootNode;
        rootNode.name     = rootPath;
        rootNode.fullPath = rootPath;
        rootNode.isDir    = true;
        rootNode.loaded   = false;
        d.nodes.push_back(rootNode);
        d.htiRoot = Ne_FtpTreeAddItem(d.hwndTree, TVI_ROOT, rootPath, true, 0,
                                       d.iFolderClosed, d.iFolderOpen);
        TreeView_Expand(d.hwndTree, d.htiRoot, TVE_EXPAND);
    }

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    DeleteObject(hFont);

    return d.pendingSaveRemotePath; // empty = cancelled
}

static void ShowNsbCreditsDialog(HWND parent)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc2 = {}; wc2.lpfnWndProc = NsbAboutWndProc; wc2.hInstance = hi;
    wc2.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc2.lpszClassName = L"NsbCreditsClass";
    if (!GetClassInfoW(hi, wc2.lpszClassName, &wc2)) RegisterClassW(&wc2);

    const int W = S(480), H = S(500);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NsbCreditsClass", L"Credits",
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) return;

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    LoadLibraryW(L"Msftedit.dll");
    RECT rcC; GetClientRect(dlg, &rcC);
    const int PAD = S(10), BTN_H = S(30);
    int editH = rcC.bottom - 2*PAD - BTN_H - PAD;

    HWND hEdit = CreateWindowExW(0, L"RICHEDIT50W", NULL,
        WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,
        PAD, PAD, rcC.right-2*PAD, editH, dlg, (HMENU)200, hi, NULL);
    if (!hEdit) { DestroyWindow(dlg); return; }
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0);

    // ── Scintilla ─────────────────────────────────────────────────────────────
    AppendNsbRich(hEdit,
        L"=================================================\r\n",
        false, RGB(100,100,100), 9, true);
    AppendNsbRich(hEdit, L"SCINTILLA\r\n",  true,  RGB(0,128,64),  18, true);
    AppendNsbRich(hEdit,
        L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n",
        false, RGB(0,128,64), 0, true);
    AppendNsbRich(hEdit,
        L"Scintilla is a free, open-source source code editing component "
        L"by Neil Hodgson. NSBEdit uses Scintilla for syntax-highlighted "
        L"code editing.\r\n\r\n",
        false, RGB(40,40,40), 0, false);
    AppendNsbRich(hEdit, L"https://www.scintilla.org/\r\n", false, RGB(0,80,160), 0, false);

    // ── Lexilla ───────────────────────────────────────────────────────────────
    AppendNsbRich(hEdit,
        L"\r\n=================================================\r\n",
        false, RGB(100,100,100), 9, true);
    AppendNsbRich(hEdit, L"LEXILLA\r\n",    true,  RGB(0,80,160),  18, true);
    AppendNsbRich(hEdit,
        L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n",
        false, RGB(0,80,160), 0, true);
    AppendNsbRich(hEdit,
        L"Lexilla is a library of lexers for Scintilla, also by Neil Hodgson. "
        L"NSBEdit uses Lexilla to provide syntax highlighting for over 20 "
        L"programming languages.\r\n\r\n",
        false, RGB(40,40,40), 0, false);
    AppendNsbRich(hEdit, L"https://www.scintilla.org/Lexilla.html\r\n", false, RGB(0,80,160), 0, false);

    // ── GDI+ ─────────────────────────────────────────────────────────────────
    AppendNsbRich(hEdit,
        L"\r\n=================================================\r\n",
        false, RGB(100,100,100), 9, true);
    AppendNsbRich(hEdit, L"GDI+\r\n",       true,  RGB(0,70,140),  18, true);
    AppendNsbRich(hEdit,
        L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n",
        false, RGB(0,70,140), 0, true);
    AppendNsbRich(hEdit,
        L"GDI+ is part of the Windows platform SDK. NSBEdit uses it to render "
        L"the NSBEdit logo in the About dialog.\r\n",
        false, RGB(40,40,40), 0, false);

    // ── MinGW-W64 ─────────────────────────────────────────────────────────────
    AppendNsbRich(hEdit,
        L"\r\n=================================================\r\n",
        false, RGB(100,100,100), 9, true);
    AppendNsbRich(hEdit, L"MINGW-W64\r\n", true, RGB(160,82,45), 18, true);
    AppendNsbRich(hEdit,
        L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n",
        false, RGB(160,82,45), 0, true);
    AppendNsbRich(hEdit,
        L"The MinGW-W64 project provides a complete runtime environment for "
        L"GCC and LLVM/Clang toolchains on Windows. NSBEdit is compiled with "
        L"MinGW-W64 GCC 15.2.0.\r\n\r\n",
        false, RGB(40,40,40), 0, false);
    AppendNsbRich(hEdit, L"https://www.mingw-w64.org/\r\n", false, RGB(0,80,160), 0, false);

    // ── SQLite3 ───────────────────────────────────────────────────────────────
    AppendNsbRich(hEdit,
        L"\r\n=================================================\r\n",
        false, RGB(100,100,100), 9, true);
    AppendNsbRich(hEdit, L"SQLITE3\r\n", true, RGB(0,100,80), 18, true);
    AppendNsbRich(hEdit,
        L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n",
        false, RGB(0,100,80), 0, true);
    AppendNsbRich(hEdit,
        L"SQLite is a small, fast, self-contained SQL database engine in the public domain. "
        L"NSBEdit uses the SQLite amalgamation to store FTP/SFTP connection profiles.\r\n\r\n",
        false, RGB(40,40,40), 0, false);
    AppendNsbRich(hEdit, L"https://www.sqlite.org/\r\n", false, RGB(0,80,160), 0, false);

    // ── libcurl + libssh2 ─────────────────────────────────────────────────────
    AppendNsbRich(hEdit,
        L"\r\n=================================================\r\n",
        false, RGB(100,100,100), 9, true);
    AppendNsbRich(hEdit, L"LIBCURL / LIBSSH2\r\n", true, RGB(0,60,180), 18, true);
    AppendNsbRich(hEdit,
        L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n",
        false, RGB(0,60,180), 0, true);
    AppendNsbRich(hEdit,
        L"libcurl is a free, open-source URL transfer library by Daniel Stenberg. "
        L"libssh2 is an open-source SSH2 protocol library. "
        L"NSBEdit uses both to implement FTP and SFTP file transfer for remote editing.\r\n\r\n",
        false, RGB(40,40,40), 0, false);
    AppendNsbRich(hEdit, L"https://curl.se/\r\n", false, RGB(0,80,160), 0, false);
    AppendNsbRich(hEdit, L"https://libssh2.org/\r\n", false, RGB(0,80,160), 0, false);

    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);

    NeDialogData credDD = {};
    credDD.buttonCount = 1;
    credDD.buttons[0] = { IDOK, Ls(L"BTN_CLOSE"), NeBtnTone::Red, IDI_ERROR,
                          Ne_MeasureButtonWidth(Ls(L"BTN_CLOSE")) };
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&credDD);

    int bw = credDD.buttons[0].width;
    int bx = (rcC.right - bw) / 2, by = rcC.bottom - PAD - BTN_H;
    HWND hBtn = CreateWindowExW(0, L"BUTTON", credDD.buttons[0].text.c_str(),
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
        bx, by, bw, BTN_H, dlg, (HMENU)IDOK, hi, NULL);
    {
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(hBtn, L"NePrevProc", (HANDLE)prev);
    }

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(hBtn);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
}

static void ShowNsbAboutDialog(HWND parent)
{
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdipToken;
    Gdiplus::GdiplusStartup(&gdipToken, &gsi, NULL);

    // Load NSB.png from exe directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* sl = wcsrchr(exePath, L'\\'); if (sl) *(sl+1) = 0;
    wchar_t pngPath[MAX_PATH];
    wcscpy_s(pngPath, exePath); wcscat_s(pngPath, L"NSB.png");
    s_nas.logo = Gdiplus::Image::FromFile(pngPath);

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {}; wc.lpfnWndProc = NsbAboutWndProc; wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = L"NsbAboutClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) RegisterClassW(&wc);

    const int W = S(480), H = S(520);
    RECT pr = {}; if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int x = (pr.left+pr.right)/2 - W/2, y = (pr.top+pr.bottom)/2 - H/2;
    if (y < 30) y = 30;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE,
        L"NsbAboutClass", Ls(L"ABOUT_TITLE"),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) { Gdiplus::GdiplusShutdown(gdipToken); return; }

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APPICON));
    if (hIco) { SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
                SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIco); }

    LoadLibraryW(L"Msftedit.dll");
    RECT rcC; GetClientRect(dlg, &rcC);
    const int PAD = S(10), BTN_H = S(30);
    int editH = rcC.bottom - 3*PAD - BTN_H;

    HWND hEdit = CreateWindowExW(0, L"RICHEDIT50W", NULL,
        WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,
        PAD, PAD, rcC.right-2*PAD, editH, dlg, (HMENU)100, hi, NULL);
    if (!hEdit) { DestroyWindow(dlg); Gdiplus::GdiplusShutdown(gdipToken); return; }

    s_nas.origEdit = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)NsbAboutEditProc);
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0);

    // Reserve space for logo
    int logoH = s_nas.logo ? (int)(s_nas.logo->GetHeight() * 0.75f) : 0;
    int logoLines = (logoH + 20) / S(15);
    for (int i = 0; i < logoLines; i++) AppendNsbRich(hEdit, L"\r\n", false, RGB(0,0,0), 0, false);

    // Content
    AppendNsbRich(hEdit, Ls(L"ABOUT_APP_NAME"), true,  RGB(180,20,20),  16, true);
    AppendNsbRich(hEdit, Ls(L"ABOUT_SUBTITLE"), false, RGB(80,80,80),   10, true);
    AppendNsbRich(hEdit, L"\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\r\n", false, RGB(180,20,20), 0, true);

    // Load version from curver.txt next to exe
    wchar_t exeP2[MAX_PATH]; GetModuleFileNameW(NULL, exeP2, MAX_PATH);
    wchar_t* sl2 = wcsrchr(exeP2, L'\\'); if (sl2) *(sl2+1) = 0;
    wchar_t cvPath[MAX_PATH]; wcscpy_s(cvPath, exeP2); wcscat_s(cvPath, L"curver.txt");
    std::wstring published, version;
    HANDLE hcv = CreateFileW(cvPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hcv != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hcv, NULL); std::string buf(sz,'\0'); DWORD rd;
        ReadFile(hcv, buf.data(), sz, &rd, NULL); CloseHandle(hcv);
        int wn = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)rd, NULL, 0);
        std::wstring wc2(wn, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)rd, wc2.data(), wn);
        auto grab = [&](const wchar_t* key) -> std::wstring {
            size_t p = wc2.find(key);
            if (p == std::wstring::npos) return {};
            p += wcslen(key);
            size_t e = wc2.find(L'\n', p);
            std::wstring v = wc2.substr(p, e == std::wstring::npos ? std::wstring::npos : e-p);
            while (!v.empty() && (v.back()==L'\r'||v.back()==L'\n')) v.pop_back();
            return v;
        };
        published = grab(L"Published: ");
        version   = grab(L"Version: ");
    }
    if (published.empty()) published = L"—";
    if (version.empty())   version   = L"—";

    AppendNsbRich(hEdit, Ls(L"ABOUT_PUBLISHED"), true,  RGB(0,0,0), 0, true);
    AppendNsbRich(hEdit, (published + L"\r\n").c_str(), false, RGB(0,0,0), 0, true);
    AppendNsbRich(hEdit, Ls(L"ABOUT_VERSION"),   true,  RGB(0,0,0), 0, true);
    AppendNsbRich(hEdit, (version   + L"\r\n").c_str(), false, RGB(0,0,0), 0, true);
    AppendNsbRich(hEdit, L"\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\r\n\r\n", false, RGB(180,20,20), 0, true);
    AppendNsbRich(hEdit, Ls(L"ABOUT_DESC"),    false, RGB(40,40,40), 0, false);
    AppendNsbRich(hEdit, Ls(L"ABOUT_LICENSE"), true,  RGB(0,70,140), 0, false);

    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);

    // Buttons: View License | Credits | Close  (all owner-drawn)
    NeDialogData abtDD = {};
    abtDD.buttonCount = 3;
    abtDD.buttons[0] = { 1001, Ls(L"ABOUT_BTN_LICENSE"), NeBtnTone::Blue,  IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"ABOUT_BTN_LICENSE")) };
    abtDD.buttons[1] = { 1002, Ls(L"ABOUT_BTN_CREDITS"), NeBtnTone::Green, IDI_INFORMATION,
                         Ne_MeasureButtonWidth(Ls(L"ABOUT_BTN_CREDITS")) };
    abtDD.buttons[2] = { IDOK, Ls(L"ABOUT_BTN_CLOSE"),   NeBtnTone::Red,   IDI_ERROR,
                         Ne_MeasureButtonWidth(Ls(L"ABOUT_BTN_CLOSE")) };
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&abtDD);

    const int BG = S(8);
    int totalBW = abtDD.buttons[0].width + BG + abtDD.buttons[1].width + BG + abtDD.buttons[2].width;
    int bx = (rcC.right - totalBW) / 2, by = rcC.bottom - PAD - BTN_H;
    HWND btnHwnds[3] = {};
    int bxCur = bx;
    for (int i = 0; i < 3; ++i) {
        btnHwnds[i] = CreateWindowExW(0, L"BUTTON", abtDD.buttons[i].text.c_str(),
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            bxCur, by, abtDD.buttons[i].width, BTN_H,
            dlg, (HMENU)(UINT_PTR)abtDD.buttons[i].id, hi, NULL);
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(btnHwnds[i], GWLP_WNDPROC, (LONG_PTR)Ne_BtnHoverProc);
        SetPropW(btnHwnds[i], L"NePrevProc", (HANDLE)prev);
        bxCur += abtDD.buttons[i].width + BG;
    }

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(btnHwnds[2]); // Close button
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }

    if (s_nas.logo) { delete s_nas.logo; s_nas.logo = nullptr; }
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    Gdiplus::GdiplusShutdown(gdipToken);
}

// ── Window procedure ───────────────────────────────────────────────────────────
static LRESULT CALLBACK Ne_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    // ── WM_CREATE ─────────────────────────────────────────────────────────────
    case WM_CREATE: {
        CREATESTRUCTW* cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE      hInst = cs->hInstance;

        NeState* st  = new NeState{};
        st->pad      = S(8);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        // ── Register line-number gutter window class (once per process) ───────
        {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = NsbLineGutterProc;
            wc.hInstance     = hInst;
            wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
            wc.hbrBackground = NULL;
            wc.lpszClassName = L"NsbLineGutter";
            RegisterClassExW(&wc); // harmless if already registered
        }

        // ── Tooltip system ────────────────────────────────────────────────────
        InitTooltipSystem(hInst);

        // ── App icon from embedded resource ──────────────────────────────────
        {
            st->hIconLarge = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXICON),
                                               GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
            st->hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
            if (st->hIconLarge) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)st->hIconLarge);
            if (st->hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)st->hIconSmall);
        }

        // ── File menu ─────────────────────────────────────────────────────────
        HMENU hMenu  = CreateMenu();

        // ── Menu font — created before SetMenu so WM_MEASUREITEM has it ───────
        if (!g_hMenuFont) {
            LOGFONTW lf = {};
            lf.lfHeight  = -MulDiv(12, GetDpiForWindow(hwnd), 72);
            lf.lfQuality = CLEARTYPE_QUALITY;
            lf.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            g_hMenuFont = CreateFontIndirectW(&lf);
        }
        if (!g_hFtpMenuIcon)
            ExtractIconExW(L"shell32.dll", 150, NULL, &g_hFtpMenuIcon, 1);

        // ── File menu ─────────────────────────────────────────────────────────
        HMENU hFile  = CreatePopupMenu();
        Ne_AppendMenuOD(hFile, MF_STRING,    IDM_NEW,        Ls(L"MENU_NEW"));
        Ne_AppendMenuOD(hFile, MF_STRING,    IDM_OPEN,       Ls(L"MENU_OPEN"));
        Ne_AppendMenuOD(hFile, MF_STRING,    IDM_SAVE,       Ls(L"MENU_SAVE"));
        Ne_AppendMenuOD(hFile, MF_STRING,    IDM_SAVEAS,     Ls(L"MENU_SAVEAS"));
        Ne_AppendMenuOD(hFile, MF_STRING,    IDM_SAVE_TO_FTP, Ls(L"MENU_SAVE_TO_FTP"));
        Ne_AppendMenuOD(hFile, MF_SEPARATOR, 0,              NULL);
        Ne_AppendMenuOD(hFile, MF_STRING,    IDM_PRINT,      Ls(L"MENU_PRINT"));
        Ne_AppendMenuOD(hFile, MF_STRING,    IDM_EXPORT_PDF, Ls(L"MENU_EXPORT_PDF"));
        Ne_AppendMenuOD(hFile, MF_SEPARATOR, 0,              NULL);
        Ne_AppendMenuOD(hFile, MF_STRING,    IDM_EXIT,       Ls(L"MENU_EXIT"));
        Ne_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hFile, Ls(L"MENU_FILE"), true);
        // ── Edit menu ─────────────────────────────────────────────────────────
        HMENU hEdit2 = CreatePopupMenu();
        Ne_AppendMenuOD(hEdit2, MF_STRING,    IDM_UNDO,      Ls(L"MENU_UNDO"));
        Ne_AppendMenuOD(hEdit2, MF_STRING,    IDM_REDO,      Ls(L"MENU_REDO"));
        Ne_AppendMenuOD(hEdit2, MF_SEPARATOR, 0,             NULL);
        Ne_AppendMenuOD(hEdit2, MF_STRING,    IDM_CUT,       Ls(L"MENU_CUT"));
        Ne_AppendMenuOD(hEdit2, MF_STRING,    IDM_COPY,      Ls(L"MENU_COPY"));
        Ne_AppendMenuOD(hEdit2, MF_STRING,    IDM_PASTE,     Ls(L"MENU_PASTE"));
        Ne_AppendMenuOD(hEdit2, MF_SEPARATOR, 0,             NULL);
        Ne_AppendMenuOD(hEdit2, MF_STRING,    IDM_SELECTALL, Ls(L"MENU_SELECTALL"));
        Ne_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hEdit2, Ls(L"MENU_EDIT"), true);
        // ── Convert menu ──────────────────────────────────────────────────────
        HMENU hEncSub = CreatePopupMenu();
        Ne_AppendMenuOD(hEncSub, MF_STRING, IDM_ENC_UTF8,      Ls(L"MENU_ENC_UTF8"));
        Ne_AppendMenuOD(hEncSub, MF_STRING, IDM_ENC_UTF16LE,   Ls(L"MENU_ENC_UTF16LE"));
        Ne_AppendMenuOD(hEncSub, MF_SEPARATOR, 0,              NULL);
        Ne_AppendMenuOD(hEncSub, MF_STRING, IDM_ENC_WIN1252,   Ls(L"MENU_ENC_WIN1252"));
        Ne_AppendMenuOD(hEncSub, MF_STRING, IDM_ENC_ISO8859_1, Ls(L"MENU_ENC_ISO8859_1"));
        Ne_AppendMenuOD(hEncSub, MF_STRING, IDM_ENC_ANSI,      Ls(L"MENU_ENC_ANSI"));
        HMENU hConv = CreatePopupMenu();
        Ne_AppendMenuOD(hConv, MF_STRING,    IDM_CONV_TO_PLAIN, Ls(L"MENU_CONV_TO_PLAIN"));
        Ne_AppendMenuOD(hConv, MF_STRING,    IDM_CONV_TO_RTF,   Ls(L"MENU_CONV_TO_RTF"));
        Ne_AppendMenuOD(hConv, MF_SEPARATOR, 0,                NULL);
        Ne_AppendMenuOD(hConv, MF_POPUP | MF_STRING,
                        (UINT_PTR)hEncSub, Ls(L"MENU_ENCODING"));
        Ne_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hConv, Ls(L"MENU_CONVERT"), true);
        // ── Language menu ─────────────────────────────────────────────────────
        s_hLangMenu = CreatePopupMenu();
        for (int li = 0; li < NE_LANG_COUNT; ++li)
            Ne_AppendMenuOD(s_hLangMenu, MF_STRING, IDM_LANG_BASE + li, s_langs[li].name);
        Ne_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)s_hLangMenu, L"Language", true);
        // ── FTP menu ──────────────────────────────────────────────────────────
        s_hFtpMenu = CreatePopupMenu();
        Ne_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)s_hFtpMenu, Ls(L"MENU_FTP"), true);
        // ── Help menu ─────────────────────────────────────────────────────────
        HMENU hHelp = CreatePopupMenu();
        Ne_AppendMenuOD(hHelp, MF_STRING,    IDM_SHORTCUTS, Ls(L"MENU_SHORTCUTS"));
        Ne_AppendMenuOD(hHelp, MF_SEPARATOR, 0,             NULL);
        Ne_AppendMenuOD(hHelp, MF_STRING,    IDM_ABOUT,     Ls(L"MENU_ABOUT"));
        Ne_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hHelp, Ls(L"MENU_HELP"), true);
        SetMenu(hwnd, hMenu);

        // ── Load RichEdit DLL ─────────────────────────────────────────────────
        if (!s_neRtfDll) {
            s_neRtfDll = LoadLibraryW(L"Msftedit.dll");
            if (!s_neRtfDll) s_neRtfDll = LoadLibraryW(L"Riched20.dll");
        }
        // ── Register Scintilla window class ───────────────────────────────────
        Scintilla_RegisterClasses(hInst);

        WNDCLASSEXW wce = {}; wce.cbSize = sizeof(wce);
        const wchar_t* reClass =
            (s_neRtfDll && GetClassInfoExW(s_neRtfDll, L"RICHEDIT50W", &wce))
            ? L"RICHEDIT50W" : L"RichEdit20W";

        RECT rcC; GetClientRect(hwnd, &rcC);
        const int cW  = rcC.right;
        const int pad = st->pad;
        const int bSz = S(26);
        const int bG  = S(3);
        const int sG  = S(8);

        // ── Toolbar controls — created at (0,0); Ne_LayoutToolbar positions them ──
        const int wXs  = bSz + S(4);
        const int wAl  = bSz + S(4);
        const int wCol = bSz + S(10);

        HWND hBold = CreateWindowExW(0, L"BUTTON", L"B",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, bSz, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_BOLD, hInst, NULL);

        HWND hItalic = CreateWindowExW(0, L"BUTTON", L"I",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, bSz, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ITALIC, hInst, NULL);

        HWND hUnder = CreateWindowExW(0, L"BUTTON", L"U",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, bSz, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_UNDERLINE, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"S\u0336",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, bSz, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_STRIKE, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"X\u2082",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wXs, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_SUBSCRIPT, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"X\u00B2",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wXs, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_SUPERSCRIPT, hInst, NULL);

        HWND hFace = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            0, 0, S(170), S(280), hwnd, (HMENU)(UINT_PTR)IDC_NE_FONTFACE, hInst, NULL);
        {
            std::vector<std::wstring> fonts;
            LOGFONTW lf = {}; lf.lfCharSet = DEFAULT_CHARSET;
            HDC hdc = GetDC(hwnd);
            EnumFontFamiliesExW(hdc, &lf, Ne_FontEnumProc, (LPARAM)&fonts, 0);
            ReleaseDC(hwnd, hdc);
            // Sort alphabetically, remove duplicates
            std::sort(fonts.begin(), fonts.end(), [](const std::wstring& a, const std::wstring& b){
                return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });
            fonts.erase(std::unique(fonts.begin(), fonts.end(), [](const std::wstring& a, const std::wstring& b){
                return _wcsicmp(a.c_str(), b.c_str()) == 0;
            }), fonts.end());
            // Segoe UI pinned at top (index 0) as the default, then the rest alphabetically
            const wchar_t* defFace = L"Segoe UI";
            SendMessageW(hFace, CB_ADDSTRING, 0, (LPARAM)defFace);
            for (auto& fn : fonts)
                if (_wcsicmp(fn.c_str(), defFace) != 0)
                    SendMessageW(hFace, CB_ADDSTRING, 0, (LPARAM)fn.c_str());
            SendMessageW(hFace, CB_SETCURSEL, 0, 0);
        }

        HWND hSzCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            0, 0, S(56), S(280), hwnd, (HMENU)(UINT_PTR)IDC_NE_FONTSIZE, hInst, NULL);
        for (int i = 0; i < s_neFontCount; i++) {
            wchar_t sz[8]; swprintf_s(sz, L"%d", s_neFontSizes[i]);
            SendMessageW(hSzCombo, CB_ADDSTRING, 0, (LPARAM)sz);
        }
        SendMessageW(hSzCombo, CB_SETCURSEL, s_neFontDefault, 0);

        CreateWindowExW(0, L"BUTTON", L"\u2261L",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wAl, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ALIGN_L, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"\u2261C",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wAl, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ALIGN_C, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"\u2261R",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wAl, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ALIGN_R, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"\u2261J",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wAl, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ALIGN_J, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"\u2022",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wAl, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_BULLET, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"1.",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wAl, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_NUMBERED, hInst, NULL);

        HWND hColor = CreateWindowExW(0, L"BUTTON", L"A\u25BC",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_COLOR, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"H\u25BC",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_HIGHLIGHT, hInst, NULL);

        HWND hImgBtn = CreateWindowExW(0, L"BUTTON", L"\U0001F5BC",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_IMAGE, hInst, NULL);

        // ── New toolbar controls ──────────────────────────────────────────────
        CreateWindowExW(0, L"BUTTON", L"\u21E5",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wXs, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_INDENT_IN, hInst, NULL);

        CreateWindowExW(0, L"BUTTON", L"\u21E4",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wXs, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_INDENT_OUT, hInst, NULL);

        HWND hLineSpBtn = CreateWindowExW(0, L"BUTTON", L"\u2195",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wXs, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_LINESPACE, hInst, NULL);

        HWND hFindBtn = CreateWindowExW(0, L"BUTTON", L"\u2315",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_FIND, hInst, NULL);

        HWND hLinkBtn = CreateWindowExW(0, L"BUTTON", L"\U0001F517",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_LINK, hInst, NULL);

        HWND hTableBtn = CreateWindowExW(0, L"BUTTON", L"\u229E",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol - S(13), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_TABLE, hInst, NULL);
        HWND hTableDrop = CreateWindowExW(0, L"BUTTON", L"\u25BC",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, S(13), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_TABLE_DROP, hInst, NULL);

        HWND hHlineBtn = CreateWindowExW(0, L"BUTTON", L"\u2500",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_HLINE, hInst, NULL);

        HWND hClearFmtBtn = CreateWindowExW(0, L"BUTTON", L"\u2205",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_CLEARFMT, hInst, NULL);

        HWND hPrintBtn = CreateWindowExW(0, L"BUTTON", L"\u2399",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wCol, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_PRINT_BTN, hInst, NULL);

        // Zoom combobox.
        HWND hZoom = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            0, 0, S(72), S(200), hwnd, (HMENU)(UINT_PTR)IDC_NE_ZOOM, hInst, NULL);
        {
            const wchar_t* zooms[] = { L"50%", L"75%", L"100%", L"125%", L"150%", L"200%" };
            for (auto z : zooms) SendMessageW(hZoom, CB_ADDSTRING, 0, (LPARAM)z);
            SendMessageW(hZoom, CB_SETCURSEL, 2, 0); // default 100%
        }

        HWND hWrapBtn = CreateWindowExW(0, L"BUTTON", L"\u21B5",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wXs, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_WORDWRAP, hInst, NULL);
        SendMessageW(hWrapBtn, BM_SETCHECK, BST_CHECKED, 0); // word wrap on by default

        HWND hCaseBtn = CreateWindowExW(0, L"BUTTON", L"Aa",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wXs, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_CASE, hInst, NULL);

        // IDC_BTN_LINENUM toolbar button removed — toggle lives in the gutter strip

        HWND hParSpBtn = CreateWindowExW(0, L"BUTTON", L"\u00B6\u2195",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            0, 0, wXs, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_PARSPACE, hInst, NULL);

        // ── Status bar ────────────────────────────────────────────────────────
        HWND hSb = NeStatusBar_Create(hwnd, IDC_NE_STATUSBAR, hInst);
        NeStatusBar_SetLabels(hSb,
            Ls(L"SB_WORDS"), Ls(L"SB_CHARS"),
            Ls(L"SB_SAVED"), Ls(L"SB_UNSAVED"));
        {
            RECT rcSb; GetClientRect(hSb, &rcSb);
            st->statusH = rcSb.bottom > 0 ? rcSb.bottom : S(22);
        }

        // ── Tab strip + toolbar layout ──────────────────────────────────────────
        st->tabY = pad;
        st->tabH = S(30);
        int toolbarTop = st->tabY + st->tabH + S(4);
        int editY = Ne_LayoutToolbar(hwnd, cW, toolbarTop);
        st->editY = editY;
        int editH = rcC.bottom - editY - st->statusH - S(2);

        NeTabsCreateParams tp = {};
        tp.hwndParent = hwnd;
        tp.hInst = hInst;
        tp.richEditClass = reClass;
        tp.tabCtrlId = IDC_NE_TABCTRL;
        tp.editCtrlId = IDC_NE_EDIT;
        tp.pad = pad;
        tp.tabHeight = st->tabH;
        tp.untitledLabel = Ls(L"UNTITLED");
        NeTabs_Create(tp);
        NeTabs_SetContextLabels(hwnd, Ls(L"TAB_CTX_NEW_TAB"), Ls(L"TAB_CTX_CLOSE_TAB"));
        st->editX = pad; st->editW = cW - 2 * pad; st->editH = std::max(1, editH);
        NeTabs_SetRects(hwnd,
            pad, st->tabY, cW - 2 * pad, st->tabH,
            pad, editY, cW - 2 * pad, std::max(1, editH));

        HWND hEdit = NeTabs_GetActiveEdit(hwnd);
        if (hEdit) {
            // Default font: Segoe UI 12pt, auto colour.
            CHARFORMAT2W cfD = {}; cfD.cbSize = sizeof(cfD);
            cfD.dwMask    = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR | CFM_EFFECTS;
            cfD.dwEffects = CFE_AUTOCOLOR;
            cfD.yHeight   = s_neFontSizes[s_neFontDefault] * 20;
            cfD.bCharSet  = DEFAULT_CHARSET;
            wcsncpy_s(cfD.szFaceName, L"Segoe UI", LF_FACESIZE - 1);
            SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfD);
            SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
            SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_LINK | ENM_SCROLL);
            Ne_SubclassEditForCaret(hEdit);
            SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_SELCHANGE);  // suppress EN_CHANGE during attach
            Ne_AttachScrollbars(hEdit);
            SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_LINK | ENM_SCROLL);
        }

        Ne_SyncScrollbarVisibility(hwnd);
        Ne_SyncRichGutters(hwnd);

        // ── Apply system font to toolbar buttons / combos ─────────────────────
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        ncm.lfMessageFont.lfHeight = -MulDiv(12, GetDpiForWindow(hwnd), 72);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
        if (hF) {
            auto applyFont = [](HWND hC, LPARAM lp) -> BOOL {
                wchar_t cls[64] = {};
                GetClassNameW(hC, cls, 64);
                if (_wcsicmp(cls, L"RichEdit20W") != 0 &&
                    _wcsicmp(cls, L"RICHEDIT50W") != 0)
                    SendMessageW(hC, WM_SETFONT, lp, TRUE);
                return TRUE;
            };
            EnumChildWindows(hwnd, applyFont, (LPARAM)hF);

            // Bold/Italic/Strike variants.
            NONCLIENTMETRICSW ncmB = ncm; ncmB.lfMessageFont.lfWeight = FW_BOLD;
            HFONT hFB = CreateFontIndirectW(&ncmB.lfMessageFont);
            if (hFB) { SendMessageW(hBold, WM_SETFONT, (WPARAM)hFB, TRUE); SetPropW(hwnd, L"neFontBold", hFB); }

            NONCLIENTMETRICSW ncmI = ncm; ncmI.lfMessageFont.lfItalic = TRUE;
            HFONT hFI = CreateFontIndirectW(&ncmI.lfMessageFont);
            if (hFI) { SendMessageW(hItalic, WM_SETFONT, (WPARAM)hFI, TRUE); SetPropW(hwnd, L"neFontItalic", hFI); }

            NONCLIENTMETRICSW ncmS = ncm; ncmS.lfMessageFont.lfStrikeOut = TRUE;
            HFONT hFS = CreateFontIndirectW(&ncmS.lfMessageFont);
            if (hFS) {
                SendMessageW(GetDlgItem(hwnd, IDC_NE_STRIKE), WM_SETFONT, (WPARAM)hFS, TRUE);
                SetPropW(hwnd, L"neFontStrike", hFS);
            }
            SetPropW(hwnd, L"neFont", hF);
        }

        // ── Tooltips (English only) ───────────────────────────────────────────
        Ne_SetTip(hBold,                                    Ls(L"TIP_BOLD"));
        Ne_SetTip(hItalic,                                  Ls(L"TIP_ITALIC"));
        Ne_SetTip(hUnder,                                   Ls(L"TIP_UNDERLINE"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_STRIKE),         Ls(L"TIP_STRIKE"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_SUBSCRIPT),      Ls(L"TIP_SUBSCRIPT"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_SUPERSCRIPT),    Ls(L"TIP_SUPERSCRIPT"));
        Ne_SetTip(hFace,                                    Ls(L"TIP_FONTFACE"));
        Ne_SetTip(hSzCombo,                                 Ls(L"TIP_FONTSIZE"));
        Ne_SetTip(hColor,                                   Ls(L"TIP_COLOR"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_HIGHLIGHT),      Ls(L"TIP_HIGHLIGHT"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_ALIGN_L),        Ls(L"TIP_ALIGN_L"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_ALIGN_C),        Ls(L"TIP_ALIGN_C"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_ALIGN_R),        Ls(L"TIP_ALIGN_R"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_ALIGN_J),        Ls(L"TIP_ALIGN_J"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_BULLET),         Ls(L"TIP_BULLET"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_NUMBERED),       Ls(L"TIP_NUMBERED"));
        Ne_SetTip(hImgBtn,                                  Ls(L"TIP_IMAGE"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_INDENT_IN),      Ls(L"TIP_INDENT_IN"));
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_INDENT_OUT),     Ls(L"TIP_INDENT_OUT"));
        Ne_SetTip(hLineSpBtn,                               Ls(L"TIP_LINESPACE"));
        Ne_SetTip(hFindBtn,                                 Ls(L"TIP_FIND"));
        Ne_SetTip(hLinkBtn,                                 Ls(L"TIP_LINK"));
        Ne_SetTip(hTableBtn,                                Ls(L"TIP_TABLE"));
        Ne_SetTip(hTableDrop,                               Ls(L"TIP_TABLE_DROP"));
        Ne_SetTip(hHlineBtn,                                Ls(L"TIP_HLINE"));
        Ne_SetTip(hClearFmtBtn,                             Ls(L"TIP_CLEARFMT"));
        Ne_SetTip(hPrintBtn,                                Ls(L"TIP_PRINT_BTN"));
        Ne_SetTip(hZoom,                                    Ls(L"TIP_ZOOM"));
        Ne_SetTip(hWrapBtn,                                 Ls(L"TIP_WORDWRAP"));
        Ne_SetTip(hCaseBtn,                                 Ls(L"TIP_CASE"));
        Ne_SetTip(hParSpBtn,                                Ls(L"TIP_PARSPACE"));

        if (hEdit) SetFocus(hEdit);
        if (hEdit) Ne_SyncToolbar(hwnd, hEdit);
        NeTabs_UpdateAllTitles(hwnd);
        return 0;
    }

    // ── WM_SIZE ───────────────────────────────────────────────────────────────
    case WM_SIZE: {
        NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!st) break;
        int cW = LOWORD(lParam), cH = HIWORD(lParam);
        int pad = st->pad;

        // Reposition status bar.
        HWND hSb = GetDlgItem(hwnd, IDC_NE_STATUSBAR);
        if (hSb) {
            int sbH = S(22);
            SetWindowPos(hSb, NULL, 0, cH - sbH, cW, sbH, SWP_NOZORDER | SWP_NOACTIVATE);
            st->statusH = sbH;
        }

        // Re-layout toolbar (switches between one/two rows as window is resized).
        int toolbarTop = st->tabY + st->tabH + S(4);
        int newEditY = Ne_LayoutToolbar(hwnd, cW, toolbarTop);
        st->editY = newEditY;

        int editH  = cH - newEditY - st->statusH - S(2);
        if (editH > 0) {
            int editX = pad, editW = cW - 2 * pad;
            st->editX = editX; st->editW = editW; st->editH = editH;
            NeTabs_SetRects(hwnd,
                pad, st->tabY, cW - 2 * pad, st->tabH,
                editX, newEditY, editW, editH);
            Ne_SyncRichGutters(hwnd);
            Ne_SyncScrollbarVisibility(hwnd);
        }
        return 0;
    }

    // ── WM_COMMAND ────────────────────────────────────────────────────────────
    case WM_COMMAND: {
        NeState* st    = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        HWND     hEdit = NeTabs_GetActiveEdit(hwnd);
        int wmId  = LOWORD(wParam);
        int wmEv  = HIWORD(wParam);

        // ── File menu ─────────────────────────────────────────────────────────
        if (wmId == IDM_CTX_TABLE_PROPS) {
            if (hEdit) Ne_ShowTablePropsDialog(hwnd, hEdit);
            return 0;
        }
        if (wmId == IDM_CTX_HRULE_PROPS) {
            if (hEdit) Ne_EditHRuleProps(hwnd, hEdit);
            return 0;
        }
        if (wmId == IDM_NEW || wmId == IDC_NE_TABCTRL + 10) { Ne_New(hwnd); return 0; }
        if (wmId == IDM_OPEN)       { Ne_Open(hwnd);              return 0; }
        if (wmId == IDM_SAVE)       { Ne_Save(hwnd);              return 0; }
        if (wmId == IDM_SAVEAS)     { Ne_SaveAs(hwnd);            return 0; }
        if (wmId == IDM_SAVE_TO_FTP) {
            // ── Save current document to an FTP server ────────────────────────
            // 1. Collect connected profiles so user can pick a server
            std::vector<NeProfile> ftpProfiles;
            NeProfiles_List(ftpProfiles);
            std::vector<NeProfile> connected;
            for (auto& p : ftpProfiles)
                if (NeFtp_IsConnected(p.id)) connected.push_back(p);

            if (connected.empty()) {
                NeDialogButtonSpec btn = { IDOK, Ls(L"BTN_OK"), NeBtnTone::Red, IDI_ERROR, 0 };
                Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_CONN_FAILED"),
                                    Ls(L"FTP_STATUS"), &btn, 1, IDOK);
                return 0;
            }

            // If exactly one connected server, use it; otherwise ask which one
            int64_t chosenId = connected[0].id;
            if (connected.size() > 1) {
                // Build a simple choice dialog listing server names
                // (Use at most 3 servers to fit the dialog button limit)
                int n = std::min((int)connected.size(), 3);
                NeDialogButtonSpec btns[3];
                for (int i = 0; i < n; ++i)
                    btns[i] = { 2000 + i, connected[i].friendlyName, NeBtnTone::Blue,
                                IDI_INFORMATION, Ne_MeasureButtonWidth(connected[i].friendlyName) };
                int r = Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_SAVE_BROWSER"),
                                            Ls(L"FTP_SAVE_BROWSER"), btns, n, IDCANCEL);
                bool found = false;
                for (int i = 0; i < n; ++i)
                    if (r == 2000 + i) { chosenId = connected[i].id; found = true; break; }
                if (!found) return 0;
            }

            // 2. Save doc to temp file
            NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
            if (!doc) return 0;
            std::wstring tempPath;
            {
                wchar_t tmp[MAX_PATH] = {};
                GetTempPathW(MAX_PATH, tmp);
                std::wstring dir = std::wstring(tmp) + L"NSBEdit_savelocal";
                CreateDirectoryW(dir.c_str(), NULL);
                // Derive suggested filename
                std::wstring fname = L"untitled";
                if (!doc->path.empty()) {
                    size_t sl = doc->path.rfind(L'\\');
                    if (sl == std::wstring::npos) sl = doc->path.rfind(L'/');
                    fname = (sl != std::wstring::npos) ? doc->path.substr(sl+1) : doc->path;
                } else if (!doc->ftpRemotePath.empty()) {
                    size_t sl = doc->ftpRemotePath.rfind(L'/');
                    fname = (sl != std::wstring::npos) ? doc->ftpRemotePath.substr(sl+1)
                                                        : doc->ftpRemotePath;
                }
                tempPath = dir + L"\\" + fname;
                if (!Ne_SaveToPath(hwnd, tempPath)) return 0;
            }
            std::wstring suggestName;
            {
                size_t sl = tempPath.rfind(L'\\');
                suggestName = (sl != std::wstring::npos) ? tempPath.substr(sl+1) : tempPath;
            }

            // 3. Open browser in save mode to pick destination
            std::wstring remoteDest = Ne_ShowFtpBrowserSave(hwnd, chosenId, suggestName);
            if (remoteDest.empty()) return 0; // cancelled

            // 4. Upload
            NeFtp_SetActiveConn(chosenId);
            SetCursor(LoadCursorW(NULL, IDC_WAIT));
            bool ok = NeFtp_Upload(tempPath, remoteDest);
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            if (!ok) {
                NeDialogButtonSpec btn = { IDOK, Ls(L"BTN_OK"), NeBtnTone::Red, IDI_ERROR, 0 };
                Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_CONN_FAILED"),
                                    NeFtp_GetLastError(), &btn, 1, IDOK);
                return 0;
            }

            // 5. Mark doc as FTP-linked and show success (connection kept open)
            if (doc) {
                doc->isFtpFile       = true;
                doc->ftpProfileId    = chosenId;
                doc->ftpRemotePath   = remoteDest;
                NeFtp_SetActiveConn(chosenId);
                doc->ftpFriendlyName = NeFtp_GetActiveProfile().friendlyName;
                doc->modified        = false;
                NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
                Ne_UpdateTitle(hwnd);
            }
            NeDialogButtonSpec okBtn = { IDOK, Ls(L"BTN_OK"), NeBtnTone::Green,
                                         IDI_INFORMATION, 0 };
            Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_SAVE_TO"),
                                Ls(L"FTP_SAVED_OK"), &okBtn, 1, IDOK);
            return 0;
        }
        if (wmId == IDM_EXIT)       { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
        if (wmId == IDM_PRINT)      { Ne_Print(hwnd);             return 0; }
        if (wmId == IDM_EXPORT_PDF) { Ne_ExportPdf(hwnd);         return 0; }
        // ── Language menu ─────────────────────────────────────────────────────
        if (wmId >= IDM_LANG_BASE && wmId < IDM_LANG_BASE + NE_LANG_COUNT) {
            int newLang = wmId - IDM_LANG_BASE;
            NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
            if (!doc) return 0;

            // If the current tab is a RichEdit (no Scintilla yet), switch it to
            // a Scintilla tab now — grab the plain text first, then create Sci.
            if (!doc->hSci) {
                HWND hEdit = doc->hEdit;
                // Extract plain text from RichEdit as wide string.
                int wlen = GetWindowTextLengthW(hEdit);
                std::wstring wide(wlen, L'\0');
                if (wlen > 0) GetWindowTextW(hEdit, wide.data(), wlen + 1);
                // Convert to UTF-8 for Scintilla.
                std::string utf8;
                if (!wide.empty()) {
                    int u8len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                                    NULL, 0, NULL, NULL);
                    if (u8len > 0) {
                        utf8.resize(u8len);
                        WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                            utf8.data(), u8len, NULL, NULL);
                    }
                }
                // Create Scintilla at the same position as hEdit.
                RECT rcEdit; GetWindowRect(hEdit, &rcEdit);
                POINT pt = { rcEdit.left, rcEdit.top };
                ScreenToClient(hwnd, &pt);
                int w = rcEdit.right - rcEdit.left;
                int h = rcEdit.bottom - rcEdit.top;
                doc->hSci = Ne_CreateScintilla(hwnd, pt.x, pt.y, w, h);
                if (!doc->hSci) return 0;
                SendMessageW(doc->hSci, SCI_SETTEXT, 0, (LPARAM)utf8.c_str());
                SendMessageW(doc->hSci, SCI_EMPTYUNDOBUFFER, 0, 0);
                SendMessageW(doc->hSci, SCI_SETSAVEPOINT, 0, 0);
                SendMessageW(doc->hSci, SCI_SETZOOM, (WPARAM)g_zoomSci, 0);
                ShowWindow(hEdit, SW_HIDE);
                ShowWindow(doc->hSci, SW_SHOW);
                SetFocus(doc->hSci);
            }

            doc->langId = newLang;
            doc->langUserSet = true;
            Ne_ApplyLang(doc->hSci, newLang);
            Ne_UpdateLangMenuCheck(newLang);
            Ne_UpdateStatusText(hwnd);
            Ne_SyncToolbar(hwnd, doc->hSci);
            return 0;
        }
        // ── Convert menu ──────────────────────────────────────────────────────
        if (wmId == IDM_CONV_TO_PLAIN) {
            HWND hEd = NeTabs_GetActiveEdit(hwnd);
            NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
            if (!hEd || !doc) return 0;
            if (MessageBoxW(hwnd, Ls(L"MSG_CONV_LOSSY"), Ls(L"APP_NAME"),
                            MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
            // Pull plain text, stream back in as plain, update path/encoding.
            std::string plain = Ne_StreamOut(hEd, false);
            doc->suppressChange = true;
            Ne_StreamIn(hEd, plain, false);
            doc->suppressChange = false;
            doc->encoding = (int)NeEncoding::UTF8;
            // If the file had a .rtf extension, change it to .txt.
            if (Ne_DocIsRtf(doc)) {
                size_t dot = doc->path.rfind(L'.');
                doc->path = (dot != std::wstring::npos ? doc->path.substr(0, dot) : doc->path) + L".txt";
            }
            doc->modified = true;
            NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
            Ne_UpdateTitle(hwnd);
            Ne_UpdateStatusText(hwnd);
            return 0;
        }
        if (wmId == IDM_CONV_TO_RTF) {
            HWND hEd = NeTabs_GetActiveEdit(hwnd);
            NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
            if (!hEd || !doc || Ne_DocIsRtf(doc)) return 0;
            // Build the .rtf path and remember the old plain path.
            std::wstring oldPath = doc->path;
            size_t dot = doc->path.rfind(L'.');
            doc->path = (dot != std::wstring::npos ? doc->path.substr(0, dot) : doc->path) + L".rtf";
            if (!oldPath.empty()) doc->prevPlainPath = oldPath;
            doc->encoding = (int)NeEncoding::RichText;
            doc->modified = true;
            NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
            Ne_UpdateTitle(hwnd);
            Ne_UpdateStatusText(hwnd);
            return 0;
        }
        if (wmId >= IDM_ENC_UTF8 && wmId <= IDM_ENC_ISO8859_1) {
            HWND hEd = NeTabs_GetActiveEdit(hwnd);
            NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
            if (!hEd || !doc) return 0;
            // Map command → NeEncoding + codepage for narrow conversion.
            NeEncoding newEnc = NeEncoding::UTF8;
            UINT cp = CP_UTF8;
            if      (wmId == IDM_ENC_UTF8)      { newEnc = NeEncoding::UTF8;      cp = CP_UTF8; }
            else if (wmId == IDM_ENC_UTF16LE)   { newEnc = NeEncoding::UTF16LE;   cp = 1200;    }
            else if (wmId == IDM_ENC_ANSI)      { newEnc = NeEncoding::ANSI;      cp = CP_ACP;  }
            else if (wmId == IDM_ENC_WIN1252)   { newEnc = NeEncoding::Win1252;   cp = 1252;    }
            else if (wmId == IDM_ENC_ISO8859_1) { newEnc = NeEncoding::ISO8859_1; cp = 28591;   }
            if ((NeEncoding)doc->encoding == newEnc) return 0; // nothing to do
            // Warn if encoding change could be lossy (anything narrower than UTF-8/16).
            if (cp != CP_UTF8 && cp != 1200) {
                if (MessageBoxW(hwnd, Ls(L"MSG_ENC_LOSSY"), Ls(L"APP_NAME"),
                                MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
            }
            doc->encoding = (int)newEnc;
            doc->modified = true;   // needs re-saving in new encoding
            Ne_UpdateStatusText(hwnd);
            return 0;
        }
        // ── Edit menu ─────────────────────────────────────────────────────────
        if (wmId == IDM_UNDO)      { if (hEdit) SendMessageW(hEdit, WM_UNDO, 0, 0);                  SetFocus(hEdit); return 0; }
        if (wmId == IDM_REDO)      { if (hEdit) SendMessageW(hEdit, EM_REDO, 0, 0);                  SetFocus(hEdit); return 0; }
        if (wmId == IDM_CUT)       { if (hEdit) SendMessageW(hEdit, WM_CUT,  0, 0);                  SetFocus(hEdit); return 0; }
        if (wmId == IDM_COPY)      { if (hEdit) SendMessageW(hEdit, WM_COPY, 0, 0);                  SetFocus(hEdit); return 0; }
        if (wmId == IDM_PASTE)     { if (hEdit) SendMessageW(hEdit, WM_PASTE, 0, 0);                 SetFocus(hEdit); return 0; }
        if (wmId == IDM_SELECTALL) { if (hEdit) SendMessageW(hEdit, EM_SETSEL, 0, -1);               SetFocus(hEdit); return 0; }
        // ── Help menu ─────────────────────────────────────────────────────────
        if (wmId == IDM_SHORTCUTS) { Ne_ShowShortcuts(hwnd);      return 0; }
        if (wmId == IDM_ABOUT)     { ShowNsbAboutDialog(hwnd);     return 0; }
        // ── FTP menu ──────────────────────────────────────────────────────────
        if (wmId == IDM_FTP_ADD_SITE) {
            Ne_ShowFtpSiteDialog(hwnd, nullptr);
            return 0;
        }
        if (wmId == IDM_FTP_DISCONNECT) {
            // Disconnect the active tab's profile, or the current active conn.
            NeTabDoc* aDoc = NeTabs_GetActiveDoc(hwnd);
            int64_t discId = (aDoc && aDoc->isFtpFile) ? aDoc->ftpProfileId : 0;
            if (discId)
                NeFtp_Disconnect(discId);
            else
                NeFtp_Disconnect();
            Ne_UpdateFtpStatus(hwnd);
            return 0;
        }
        if (wmId >= IDM_FTP_CONNECT_BASE && wmId < IDM_FTP_CONNECT_BASE + 100) {
            std::vector<NeProfile> ftpProfiles;
            NeProfiles_List(ftpProfiles);
            int ftpIdx = wmId - IDM_FTP_CONNECT_BASE;
            if (ftpIdx >= 0 && ftpIdx < (int)ftpProfiles.size()) {
                NeProfile toConnect = ftpProfiles[ftpIdx];
                // Already connected — just open the browser for this profile.
                if (NeFtp_IsConnected(toConnect.id)) {
                    NeFtp_SetActiveConn(toConnect.id);
                    Ne_UpdateFtpStatus(hwnd);
                    Ne_ShowFtpBrowser(hwnd, toConnect.id);
                    return 0;
                }
                // Prompt for password if not saved
                if (!toConnect.rememberPassword || toConnect.password.empty()) {
                    std::wstring pw;
                    if (!Ne_ShowPasswordPrompt(hwnd, toConnect.friendlyName, pw))
                        return 0;
                    toConnect.password = pw;
                }
                SetCursor(LoadCursorW(NULL, IDC_WAIT));
                bool connected = NeFtp_Connect(toConnect);
                SetCursor(LoadCursorW(NULL, IDC_ARROW));
                if (connected) {
                    Ne_UpdateFtpStatus(hwnd);
                    Ne_ShowFtpBrowser(hwnd, toConnect.id);
                } else {
                    NeDialogButtonSpec okBtn = { IDOK, Ls(L"BTN_OK"), NeBtnTone::Blue,
                                                 IDI_ERROR, 0 };
                    Ne_ShowChoiceDialog(hwnd, Ls(L"FTP_CONN_FAILED"),
                                        NeFtp_GetLastError(), &okBtn, 1, IDOK);
                }
            }
            return 0;
        }

        if (!hEdit) break;

        // ── Character formatting buttons ──────────────────────────────────────
        if (wmId == IDC_NE_BOLD) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleEffect(hEdit, CFM_BOLD, CFE_BOLD);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_ITALIC) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleEffect(hEdit, CFM_ITALIC, CFE_ITALIC);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_UNDERLINE) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleEffect(hEdit, CFM_UNDERLINE, CFE_UNDERLINE);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_STRIKE) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleEffect(hEdit, CFM_STRIKEOUT, CFE_STRIKEOUT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_SUBSCRIPT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleScript(hEdit, CFE_SUBSCRIPT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_SUPERSCRIPT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleScript(hEdit, CFE_SUPERSCRIPT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }

        // ── Text colour ───────────────────────────────────────────────────────
        if (wmId == IDC_NE_COLOR) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            static COLORREF s_custColors[16] = {};
            CHOOSECOLORW cc = {}; cc.lStructSize = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = s_custColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            CHARFORMAT2W cfC = {}; cfC.cbSize = sizeof(cfC); cfC.dwMask = CFM_COLOR;
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfC);
            cc.rgbResult = (cfC.dwEffects & CFE_AUTOCOLOR) ? RGB(0,0,0) : cfC.crTextColor;
            if (ChooseColorW(&cc)) {
                CHARFORMAT2W cfSet = {}; cfSet.cbSize = sizeof(cfSet);
                cfSet.dwMask = CFM_COLOR; cfSet.dwEffects = 0;
                cfSet.crTextColor = cc.rgbResult;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSet);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── Highlight colour ──────────────────────────────────────────────────
        if (wmId == IDC_NE_HIGHLIGHT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            static COLORREF s_hlColors[16] = {};
            CHOOSECOLORW cc = {}; cc.lStructSize = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = s_hlColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            cc.rgbResult    = RGB(255, 255, 0);
            CHARFORMAT2W cfH = {}; cfH.cbSize = sizeof(cfH); cfH.dwMask = CFM_BACKCOLOR;
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfH);
            if (!(cfH.dwEffects & CFE_AUTOBACKCOLOR)) cc.rgbResult = cfH.crBackColor;
            if (ChooseColorW(&cc)) {
                CHARFORMAT2W cfSet = {}; cfSet.cbSize = sizeof(cfSet);
                cfSet.dwMask    = CFM_BACKCOLOR; cfSet.dwEffects = 0;
                cfSet.crBackColor = cc.rgbResult;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSet);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── Alignment ─────────────────────────────────────────────────────────
        if (wmId == IDC_NE_ALIGN_L) { Ne_SetAlignment(hEdit, PFA_LEFT);    Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_ALIGN_C) { Ne_SetAlignment(hEdit, PFA_CENTER);  Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_ALIGN_R) { Ne_SetAlignment(hEdit, PFA_RIGHT);   Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_ALIGN_J) { Ne_SetAlignment(hEdit, PFA_JUSTIFY); Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }

        // ── Lists ─────────────────────────────────────────────────────────────
        if (wmId == IDC_NE_BULLET)   { Ne_ToggleBullet(hEdit);   Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_NUMBERED) { Ne_ToggleNumbered(hEdit); Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }

        // ── Insert image ──────────────────────────────────────────────────────
        if (wmId == IDC_NE_IMAGE) { Ne_InsertImage(hwnd, hEdit); return 0; }

        // ── New toolbar handlers ───────────────────────────────────────────────
        if (wmId == IDC_NE_INDENT_IN)  { Ne_Indent(hEdit, true);  SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_INDENT_OUT) { Ne_Indent(hEdit, false); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_LINESPACE)  { Ne_ShowLineSpaceDialog(hwnd, hEdit); return 0; }
        if (wmId == IDC_NE_PARSPACE)   { Ne_ShowParSpaceDialog(hwnd, hEdit);  return 0; }
        if (wmId == IDC_NE_FIND)       { Ne_ShowFindDialog(hwnd, hEdit);      return 0; }
        if (wmId == IDC_NE_LINK)       { Ne_ShowLinkDialog(hwnd, hEdit);      return 0; }
        if (wmId == IDC_NE_TABLE) {
            Ne_ShowTablePropsDialog(hwnd, hEdit);
            return 0;
        }
        if (wmId == IDC_NE_TABLE_DROP) {
            HWND hBtn = GetDlgItem(hwnd, IDC_NE_TABLE_DROP);
            Ne_ShowTablePicker(hwnd, hEdit, hBtn);
            return 0;
        }
        if (wmId == IDC_NE_HLINE)      { Ne_InsertHRule(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_CLEARFMT)   { Ne_ClearFormatting(hEdit); Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_PRINT_BTN)  { SendMessageW(hwnd, WM_COMMAND, IDM_PRINT, 0); return 0; }
        if (wmId == IDC_NE_WORDWRAP)   { Ne_ToggleWordWrap(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_BTN_LINENUM) {
            s_lineNumsOn = !s_lineNumsOn;
            Ne_SyncLineNumBtn(hwnd);
            HWND hAct = NeTabs_GetActiveEdit(hwnd);
            if (hAct) SetFocus(hAct);
            return 0;
        }
        if (wmId == IDC_NE_CASE)       { Ne_ToggleCase(hEdit); SetFocus(hEdit); return 0; }

        // ── Zoom combobox ─────────────────────────────────────────────────────
        if (wmId == IDC_NE_ZOOM && wmEv == CBN_SELCHANGE) {
            HWND hZ = GetDlgItem(hwnd, IDC_NE_ZOOM);
            int sel = (int)SendMessageW(hZ, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < s_zoomStepCount) {
                g_zoomRtf = s_zoomSteps[sel];
                SendMessageW(hEdit, EM_SETZOOM, (WPARAM)g_zoomRtf, 100);
                NeProfiles_SetIntSetting("zoom_rtf", g_zoomRtf);
            }
            SetFocus(hEdit); return 0;
        }

        // ── Font face ─────────────────────────────────────────────────────────
        if (wmId == IDC_NE_FONTFACE && wmEv == CBN_SELCHANGE && st && !st->updatingToolbar) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            HWND hFaceCtrl = GetDlgItem(hwnd, IDC_NE_FONTFACE);
            int sel = (int)SendMessageW(hFaceCtrl, CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                wchar_t face[LF_FACESIZE] = {};
                SendMessageW(hFaceCtrl, CB_GETLBTEXT, sel, (LPARAM)face);
                CHARFORMAT2W cfF = {}; cfF.cbSize = sizeof(cfF);
                cfF.dwMask = CFM_FACE | CFM_CHARSET;
                cfF.bCharSet = DEFAULT_CHARSET;
                wcsncpy_s(cfF.szFaceName, face, LF_FACESIZE - 1);
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfF);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── Font size ─────────────────────────────────────────────────────────
        if (wmId == IDC_NE_FONTSIZE && wmEv == CBN_SELCHANGE && st && !st->updatingToolbar) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            HWND hSzCtrl = GetDlgItem(hwnd, IDC_NE_FONTSIZE);
            int sel = (int)SendMessageW(hSzCtrl, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < s_neFontCount) {
                CHARFORMAT2W cfSz = {}; cfSz.cbSize = sizeof(cfSz);
                cfSz.dwMask  = CFM_SIZE;
                cfSz.yHeight = s_neFontSizes[sel] * 20;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSz);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── RichEdit notifications ────────────────────────────────────────────
        if (wmId == IDC_NE_EDIT) {
            HWND hSrcEdit = (HWND)lParam;
            NeTabDoc* doc = NeTabs_GetDocByEdit(hwnd, hSrcEdit);
            if (wmEv == EN_CHANGE && doc && !doc->suppressChange) {
                doc->modified = true;
                int idx = NeTabs_GetActiveIndex(hwnd);
                NeTabs_UpdateTabTitle(hwnd, idx);
                Ne_UpdateTitle(hwnd);
                Ne_UpdateStatusText(hwnd);
                // Notify custom scrollbars that content dimensions may have changed
                auto ih = s_sbH.find(hSrcEdit);
                if (ih != s_sbH.end() && ih->second) msb_notify_content_changed(ih->second);
                // Repaint line-number gutter — line count may have changed.
                if (doc->hLineGutter) InvalidateRect(doc->hLineGutter, NULL, TRUE);
            }
            if (wmEv == EN_VSCROLL) {
                // Repaint line-number gutter on vertical scroll.
                if (doc && doc->hLineGutter) InvalidateRect(doc->hLineGutter, NULL, TRUE);
            }
            if (wmEv == EN_KILLFOCUS && doc) {
                Ne_RememberDiskStamp(doc);
            }
            if (wmEv == EN_SETFOCUS) {
                Ne_CheckExternalFileChangeOnFocus(hwnd);
            }
            if (wmEv == EN_SELCHANGE && hSrcEdit == NeTabs_GetActiveEdit(hwnd)) {
                Ne_SyncToolbar(hwnd, hSrcEdit);
                Ne_UpdateStatusText(hwnd);
            }
        }
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)lParam;
        if (nh && nh->code == EN_LINK) {
            ENLINK* el = (ENLINK*)lParam;
            // EN_LINK fallback for Ctrl+Click (subclass handles primary path).
            // Use RTF stream to get the real URL from the field instruction.
            if (el->msg == WM_LBUTTONDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
                HideTooltip();
                std::wstring url = Ne_ExtractLinkUrlAt(nh->hwndFrom,
                                       (el->chrg.cpMin + el->chrg.cpMax) / 2);
                if (url.empty()) {
                    // Fall back: try display text in case it IS the URL
                    int len = el->chrg.cpMax - el->chrg.cpMin;
                    if (len > 0 && len < 2048) {
                        std::wstring disp(len + 1, L'\0');
                        TEXTRANGEW tr = {}; tr.chrg = el->chrg; tr.lpstrText = &disp[0];
                        SendMessageW(nh->hwndFrom, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
                        disp.resize(wcslen(disp.c_str()));
                        if (disp.find(L"http") == 0 || disp.find(L"www.") == 0)
                            url = disp;
                    }
                }
                if (!url.empty())
                    ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return 1;
            }
            return 0;
        }
        if (nh && nh->idFrom == IDC_NE_TABCTRL && nh->code == TCN_SELCHANGE) {
            int idx = TabCtrl_GetCurSel(NeTabs_GetTabHwnd(hwnd));
            if (NeTabs_SetActive(hwnd, idx)) {
                HWND hEditA = NeTabs_GetActiveEdit(hwnd);
                if (hEditA) Ne_SyncToolbar(hwnd, hEditA);
                Ne_SyncScrollbarVisibility(hwnd);
                Ne_SyncRichGutters(hwnd);
                Ne_UpdateStatusText(hwnd);
                Ne_UpdateTitle(hwnd);
                NeTabDoc* docA = NeTabs_GetActiveDoc(hwnd);
                Ne_UpdateLangMenuCheck(docA ? docA->langId : -1);
            }
            return 0;
        }
        // Scintilla notifications (WM_NOTIFY from child Scintilla window)
        if (nh) {
            SCNotification* scn = (SCNotification*)lParam;
            if (scn->nmhdr.code == SCN_UPDATEUI) {
                // Caret moved in the active Scintilla tab — refresh Line/Col
                HWND hSciActive = NeTabs_GetActiveScintilla(hwnd);
                if (scn->nmhdr.hwndFrom == hSciActive)
                    Ne_UpdateStatusText(hwnd);
            } else if (scn->nmhdr.code == SCN_SAVEPOINTLEFT) {
                // Document was modified after save point
                NeTabDoc* docSci = NULL;
                int n = NeTabs_GetCount(hwnd);
                for (int i = 0; i < n; ++i) {
                    NeTabDoc* d = NeTabs_GetDocByIndex(hwnd, i);
                    if (d && d->hSci == scn->nmhdr.hwndFrom) { docSci = d; break; }
                }
                if (docSci && !docSci->modified) {
                    docSci->modified = true;
                    NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
                    Ne_UpdateTitle(hwnd);
                    Ne_UpdateStatusText(hwnd);
                }
            } else if (scn->nmhdr.code == SCN_SAVEPOINTREACHED) {
                NeTabDoc* docSci = NULL;
                int n = NeTabs_GetCount(hwnd);
                for (int i = 0; i < n; ++i) {
                    NeTabDoc* d = NeTabs_GetDocByIndex(hwnd, i);
                    if (d && d->hSci == scn->nmhdr.hwndFrom) { docSci = d; break; }
                }
                if (docSci && docSci->modified) {
                    docSci->modified = false;
                    NeTabs_UpdateTabTitle(hwnd, NeTabs_GetActiveIndex(hwnd));
                    Ne_UpdateTitle(hwnd);
                    Ne_UpdateStatusText(hwnd);
                }
            } else if (scn->nmhdr.code == SCN_CHARADDED) {
                HWND hSciActive = NeTabs_GetActiveScintilla(hwnd);
                if (scn->nmhdr.hwndFrom == hSciActive) {
                    NeTabDoc* docAC = NeTabs_GetActiveDoc(hwnd);
                    if (docAC && docAC->hSci == hSciActive)
                        Ne_SciAutoComplete(hSciActive, docAC->langId);
                }
            } else if (scn->nmhdr.code == SCN_ZOOM) {
                // Ctrl+scroll on Scintilla: save new zoom level.
                HWND hSciActive = NeTabs_GetActiveScintilla(hwnd);
                if (scn->nmhdr.hwndFrom == hSciActive) {
                    g_zoomSci = (int)SendMessageW(hSciActive, SCI_GETZOOM, 0, 0);
                    NeProfiles_SetIntSetting("zoom_sci", g_zoomSci);
                }
            }
        }
        break;
    }

    case NE_WM_TABCLOSE: {
        int idx = (int)wParam;
        Ne_CloseTabAt(hwnd, idx);
        return 0;
    }

    case NE_WM_TABNEW:
        Ne_New(hwnd);
        Ne_SyncRichGutters(hwnd);
        return 0;

    // ── WM_MENURBUTTONUP — right-click on menu item ────────────────────────────
    case WM_MENURBUTTONUP: {
        HMENU hM = (HMENU)lParam;
        if (hM == s_hFtpMenu) {
            int pos = (int)wParam;
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_ID;
            if (GetMenuItemInfoW(hM, pos, TRUE, &mii)) {
                int id = (int)mii.wID;
                if (id >= IDM_FTP_CONNECT_BASE) {
                    std::vector<NeProfile> ftpProfiles;
                    NeProfiles_List(ftpProfiles);
                    int idx = id - IDM_FTP_CONNECT_BASE;
                    if (idx >= 0 && idx < (int)ftpProfiles.size()) {
                        EndMenu();
                        Ne_ShowFtpSiteDialog(hwnd, &ftpProfiles[idx]);
                    }
                }
            }
        }
        return 0;
    }

    // ── WM_CONTEXTMENU — right-click context menus ─────────────────────────────
    case WM_CONTEXTMENU: {
        HWND hSrc  = (HWND)wParam;
        HWND hEdit = NeTabs_GetActiveEdit(hwnd);
        HWND hTab  = NeTabs_GetTabHwnd(hwnd);

        // ── Right-click on the tab bar area (including [+] and empty space) ──
        // Check if hSrc is the tab control, the [+] button, or a right-click
        // in the tab-row Y band on the parent background.
        {
            bool onTabCtrl = (hSrc == hTab);
            bool onTabRow  = false;
            if (!onTabCtrl) {
                // Check if it's the [+] button (child of hwnd in tab row area)
                NeState* st2 = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
                if (st2) {
                    POINT cp = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    if (cp.x == -1 && cp.y == -1) break; // keyboard menu, skip
                    ScreenToClient(hwnd, &cp);
                    int tabY, tabH;
                    NeTabs_GetTabRowRect(hwnd, &tabY, &tabH);
                    onTabRow = (cp.y >= tabY && cp.y < tabY + tabH);
                }
            }

            if (onTabCtrl || onTabRow) {
                // Determine which tab (if any) is under the cursor
                int tabIdx = -1;
                if (hTab) {
                    POINT tp = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hTab, &tp);
                    TCHITTESTINFO hti = {}; hti.pt = tp;
                    tabIdx = TabCtrl_HitTest(hTab, &hti);
                }
                NeTabs_ShowTabContextMenu(hwnd,
                    GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), tabIdx);
                return 0;
            }
        }

        // ── Right-click on the editor ──────────────────────────────────────────
        if (hSrc != hEdit) break;

        HMENU hCtx = CreatePopupMenu();
        // Grey Undo/Redo based on whether the operation is available.
        DWORD canUndo = (DWORD)SendMessageW(hEdit, EM_CANUNDO, 0, 0);
        DWORD canRedo = (DWORD)SendMessageW(hEdit, EM_CANREDO, 0, 0);
        AppendMenuW(hCtx, MF_STRING | (canUndo ? 0 : MF_GRAYED), IDM_UNDO, Ls(L"MENU_UNDO"));
        AppendMenuW(hCtx, MF_STRING | (canRedo ? 0 : MF_GRAYED), IDM_REDO, Ls(L"MENU_REDO"));
        AppendMenuW(hCtx, MF_SEPARATOR, 0, NULL);
        // Grey Cut/Copy when nothing is selected.
        CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
        bool hasSel = (cr.cpMin != cr.cpMax);
        AppendMenuW(hCtx, MF_STRING | (hasSel ? 0 : MF_GRAYED), IDM_CUT,  Ls(L"MENU_CUT"));
        AppendMenuW(hCtx, MF_STRING | (hasSel ? 0 : MF_GRAYED), IDM_COPY, Ls(L"MENU_COPY"));
        AppendMenuW(hCtx, MF_STRING, IDM_PASTE, Ls(L"MENU_PASTE"));
        AppendMenuW(hCtx, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hCtx, MF_STRING, IDM_SELECTALL, Ls(L"MENU_SELECTALL"));
        // Table properties — shown only when caret is inside a table
        if (Ne_CaretInTable(hEdit)) {
            AppendMenuW(hCtx, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hCtx, MF_STRING, IDM_CTX_TABLE_PROPS, Ls(L"CTX_TABLE_PROPS"));
        }
        // Horizontal rule properties — shown only when caret is on a rule paragraph
        if (Ne_CaretOnHRule(hEdit)) {
            AppendMenuW(hCtx, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hCtx, MF_STRING, IDM_CTX_HRULE_PROPS, L"Horizontal Rule Properties...");
        }

        int sx = GET_X_LPARAM(lParam), sy = GET_Y_LPARAM(lParam);
        if (sx == -1 && sy == -1) {
            // Keyboard context-menu key: show near caret.
            POINT pt = {}; SendMessageW(hEdit, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
            POINTL caret = {};
            SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)&caret, cr.cpMax);
            POINT scr = { (int)caret.x, (int)caret.y };
            ClientToScreen(hEdit, &scr);
            sx = scr.x; sy = scr.y;
        }
        TrackPopupMenu(hCtx, TPM_RIGHTBUTTON, sx, sy, 0, hwnd, NULL);
        DestroyMenu(hCtx);
        return 0;
    }

    // ── WM_MEASUREITEM — size owner-draw menu items ───────────────────────────
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis->CtlType != ODT_MENU || !g_hMenuFont) break;
        NeMenuItemData* d = (NeMenuItemData*)(ULONG_PTR)mis->itemData;
        if (!d || d->isSeparator) { mis->itemHeight = S(8); mis->itemWidth = 10; return TRUE; }
        HDC hdc = GetDC(hwnd);
        HFONT hOld = (HFONT)SelectObject(hdc, g_hMenuFont);
        const wchar_t* tab = wcschr(d->text.c_str(), L'\t');
        std::wstring main = tab ? std::wstring(d->text.c_str(), tab - d->text.c_str()) : d->text;
        RECT rc = {}; DrawTextW(hdc, main.c_str(), -1, &rc, DT_CALCRECT | DT_SINGLELINE);
        int accelW = 0;
        if (tab && !d->isBar) {
            RECT ra = {}; DrawTextW(hdc, tab+1, -1, &ra, DT_CALCRECT | DT_SINGLELINE);
            accelW = ra.right + S(30);
        }
        SelectObject(hdc, hOld); ReleaseDC(hwnd, hdc);
        int hPad = d->isBar ? S(6) : S(8);
        int wPad = d->isBar ? S(20) : S(44);
        mis->itemHeight = (rc.bottom - rc.top) + hPad;
        mis->itemWidth  = (rc.right - rc.left) + wPad + accelW;
        return TRUE;
    }

    // ── WM_DRAWITEM — paint owner-draw buttons and menu items ─────────────────
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;

        // ── Owner-draw toolbar buttons ────────────────────────────────────────
        if (dis->CtlType == ODT_BUTTON) {
            int    id       = (int)dis->CtlID;
            bool   pressed  = (dis->itemState & ODS_SELECTED) != 0;
            bool   disabled = (dis->itemState & ODS_DISABLED) != 0;
            // For toggle buttons track checked state via BM_GETCHECK.
            bool   checked  = (SendMessageW(dis->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RECT   rc       = dis->rcItem;

            // Background.
            COLORREF bg;
            if      (pressed && checked) bg = RGB(155, 190, 235);
            else if (pressed)            bg = RGB(185, 215, 250);
            else if (checked)            bg = RGB(210, 230, 255);
            else                         bg = GetSysColor(COLOR_BTNFACE);

            HBRUSH hbr = CreateSolidBrush(bg);
            FillRect(dis->hDC, &rc, hbr);
            DeleteObject(hbr);

            // Border: sunken when pressed/checked, raised otherwise.
            DrawEdge(dis->hDC, &rc, (pressed || checked) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);

            // ── Special: TABLE_DROP button — draw just a tiny ▼ centred ──────
            if (id == IDC_NE_TABLE_DROP) {
                SetTextColor(dis->hDC, disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(80, 80, 80));
                SetBkMode(dis->hDC, TRANSPARENT);
                HFONT hSmall = CreateFontW(-S(8), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                    DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
                HFONT hOld2 = hSmall ? (HFONT)SelectObject(dis->hDC, hSmall) : NULL;
                RECT rcA = { rc.left + (pressed?1:0), rc.top + (pressed?1:0), rc.right, rc.bottom };
                DrawTextW(dis->hDC, L"\u25BE", -1, &rcA, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (hOld2) SelectObject(dis->hDC, hOld2);
                if (hSmall) DeleteObject(hSmall);
            } else
            // ── Special: Highlight button gets a yellow colour swatch ─────────
            if (id == IDC_NE_HIGHLIGHT) {
                // Draw "H" in normal text colour above a yellow bar.
                SetTextColor(dis->hDC, disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(30, 30, 30));
                SetBkMode(dis->hDC, TRANSPARENT);
                HFONT hf = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
                if (!hf) hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HFONT hOld = (HFONT)SelectObject(dis->hDC, hf);
                RECT rcTxt = { rc.left + (pressed?1:0), rc.top + (pressed?1:0),
                               rc.right,                rc.bottom - S(6) };
                DrawTextW(dis->hDC, L"H\u25BC", -1, &rcTxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dis->hDC, hOld);
                // Yellow swatch at bottom of button.
                RECT swRc = { rc.left + S(3), rc.bottom - S(6),
                              rc.right - S(3), rc.bottom - S(2) };
                HBRUSH hSw = CreateSolidBrush(RGB(255, 220, 0));
                FillRect(dis->hDC, &swRc, hSw);
                DeleteObject(hSw);
            } else {
                // Coloured text for all other buttons.
                wchar_t txt[64] = {};
                GetWindowTextW(dis->hwndItem, txt, 64);

                COLORREF fg = disabled ? GetSysColor(COLOR_GRAYTEXT) : Ne_BtnTextColor(id);
                SetTextColor(dis->hDC, fg);
                SetBkMode(dis->hDC, TRANSPARENT);

                HFONT hf = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
                bool  createdFont = false;
                if (!hf) {
                    // Use Segoe UI so all Unicode glyphs (◂ ▸ ↵ etc.) render correctly.
                    int ht = -(MulDiv(10, GetDeviceCaps(dis->hDC, LOGPIXELSY), 72));
                    hf = CreateFontW(ht, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                    createdFont = (hf != NULL);
                    if (!hf) hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                }
                HFONT hOld = (HFONT)SelectObject(dis->hDC, hf);

                RECT rcTxt = { rc.left + (pressed?1:0), rc.top + (pressed?1:0),
                               rc.right, rc.bottom };
                DrawTextW(dis->hDC, txt, -1, &rcTxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dis->hDC, hOld);
                if (createdFont) DeleteObject(hf);
            }
            return TRUE;
        }

        // ── Owner-draw menu items ─────────────────────────────────────────────
        if (dis->CtlType != ODT_MENU || !g_hMenuFont) break;
        NeMenuItemData* d = (NeMenuItemData*)(ULONG_PTR)dis->itemData;
        if (!d) break;
        bool selected = (dis->itemState & ODS_SELECTED) != 0;
        bool grayed   = (dis->itemState & ODS_GRAYED)   != 0;
        RECT rc = dis->rcItem;
        // Background
        COLORREF bg = selected ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 255);
        HBRUSH hbr = CreateSolidBrush(bg); FillRect(dis->hDC, &rc, hbr); DeleteObject(hbr);
        // Separator
        if (d->isSeparator) {
            int y = (rc.top + rc.bottom) / 2;
            HPEN hp = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
            HPEN op = (HPEN)SelectObject(dis->hDC, hp);
            MoveToEx(dis->hDC, rc.left + S(4), y, NULL); LineTo(dis->hDC, rc.right - S(4), y);
            SelectObject(dis->hDC, op); DeleteObject(hp);
            return TRUE;
        }
        // Text
        COLORREF fg = grayed   ? RGB(160, 160, 160)            :
                      selected ? GetSysColor(COLOR_HIGHLIGHTTEXT)  :
                                 RGB(30, 30, 30);
        SetTextColor(dis->hDC, fg);
        SetBkMode(dis->hDC, TRANSPARENT);
        // Draw gutter icon if present
        if (!d->isBar && d->hSmallIcon) {
            int iconSize = S(16);
            int ix = rc.left + (S(28) - iconSize) / 2;
            int iy = (rc.top + rc.bottom - iconSize) / 2;
            DrawIconEx(dis->hDC, ix, iy, d->hSmallIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);
        }
        HFONT hOld = (HFONT)SelectObject(dis->hDC, g_hMenuFont);
        const wchar_t* tab = wcschr(d->text.c_str(), L'\t');
        std::wstring mainTxt = tab ? std::wstring(d->text.c_str(), tab - d->text.c_str()) : d->text;
        int leftPad = d->isBar ? S(10) : S(28);
        RECT rcT = { rc.left + leftPad, rc.top, rc.right - S(6), rc.bottom };
        DrawTextW(dis->hDC, mainTxt.c_str(), -1, &rcT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (tab && !d->isBar) {
            RECT rcA = { rc.left, rc.top, rc.right - S(6), rc.bottom };
            DrawTextW(dis->hDC, tab+1, -1, &rcA, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dis->hDC, hOld);
        return TRUE;
    }

    // ── WM_INITMENUPOPUP — grey menu items dynamically ───────────────────────
    case WM_INITMENUPOPUP: {
        HMENU hPop = (HMENU)wParam;

        // ── FTP popup — rebuild from DB each time it opens ────────────────────
        if (hPop == s_hFtpMenu) {
            Ne_RebuildFtpMenu(hwnd);
            return 0;
        }

        HWND hEdit = NeTabs_GetActiveEdit(hwnd);
        if (!hEdit) break;

        // ── Edit popup ────────────────────────────────────────────────────────
        if (GetMenuState(hPop, IDM_UNDO, MF_BYCOMMAND) != (UINT)-1) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            bool hasSel  = (cr.cpMin != cr.cpMax);
            bool canUndo = SendMessageW(hEdit, EM_CANUNDO, 0, 0) != 0;
            bool canRedo = SendMessageW(hEdit, EM_CANREDO, 0, 0) != 0;
            EnableMenuItem(hPop, IDM_UNDO,  MF_BYCOMMAND | (canUndo ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(hPop, IDM_REDO,  MF_BYCOMMAND | (canRedo ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(hPop, IDM_CUT,   MF_BYCOMMAND | (hasSel  ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(hPop, IDM_COPY,  MF_BYCOMMAND | (hasSel  ? MF_ENABLED : MF_GRAYED));
            return 0;
        }

        // ── Convert popup ─────────────────────────────────────────────────────
        if (GetMenuState(hPop, IDM_CONV_TO_PLAIN, MF_BYCOMMAND) != (UINT)-1) {
            NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
            bool isRtfDoc = Ne_DocIsRtf(doc);
            // "Strip formatting" is only useful when the doc is currently RTF.
            EnableMenuItem(hPop, IDM_CONV_TO_PLAIN,
                           MF_BYCOMMAND | (isRtfDoc ? MF_ENABLED : MF_GRAYED));
            // "Add formatting" is only useful when the doc is currently plain text.
            EnableMenuItem(hPop, IDM_CONV_TO_RTF,
                           MF_BYCOMMAND | (!isRtfDoc ? MF_ENABLED : MF_GRAYED));
            return 0;
        }

        // ── Encoding sub-popup ────────────────────────────────────────────────
        if (GetMenuState(hPop, IDM_ENC_UTF8, MF_BYCOMMAND) != (UINT)-1) {
            NeTabDoc* doc = NeTabs_GetActiveDoc(hwnd);
            bool isRtfDoc = Ne_DocIsRtf(doc);
            // Encoding change is only meaningful for plain-text files.
            UINT ef = MF_BYCOMMAND | (isRtfDoc ? MF_GRAYED : MF_ENABLED);
            EnableMenuItem(hPop, IDM_ENC_UTF8,      ef);
            EnableMenuItem(hPop, IDM_ENC_UTF16LE,   ef);
            EnableMenuItem(hPop, IDM_ENC_ANSI,      ef);
            EnableMenuItem(hPop, IDM_ENC_WIN1252,   ef);
            EnableMenuItem(hPop, IDM_ENC_ISO8859_1, ef);
            // Check the currently active encoding.
            NeEncoding cur = doc ? (NeEncoding)doc->encoding : NeEncoding::Unknown;
            CheckMenuItem(hPop, IDM_ENC_UTF8,      MF_BYCOMMAND | (cur == NeEncoding::UTF8      ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(hPop, IDM_ENC_UTF16LE,   MF_BYCOMMAND | (cur == NeEncoding::UTF16LE   ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(hPop, IDM_ENC_ANSI,      MF_BYCOMMAND | (cur == NeEncoding::ANSI      ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(hPop, IDM_ENC_WIN1252,   MF_BYCOMMAND | (cur == NeEncoding::Win1252   ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(hPop, IDM_ENC_ISO8859_1, MF_BYCOMMAND | (cur == NeEncoding::ISO8859_1 ? MF_CHECKED : MF_UNCHECKED));
            return 0;
        }
        break;
    }

    // ── Fill the menu-bar gap (right of last item) with white ─────────────────
    case WM_NCPAINT: {
        LRESULT lr = DefWindowProcW(hwnd, WM_NCPAINT, wParam, 0);
        HMENU hMenu = GetMenu(hwnd);
        if (hMenu && g_hMenuFont) {
            int count = GetMenuItemCount(hMenu);
            RECT rcLast = {};
            MENUBARINFO mbi = { sizeof(mbi) };
            if (count > 0
                && GetMenuItemRect(hwnd, hMenu, count - 1, &rcLast)
                && GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
            {
                RECT rcWin; GetWindowRect(hwnd, &rcWin);
                RECT rcFill;
                rcFill.left   = rcLast.right  - rcWin.left;
                rcFill.right  = mbi.rcBar.right - rcWin.left;
                rcFill.top    = mbi.rcBar.top   - rcWin.top;
                rcFill.bottom = mbi.rcBar.bottom - rcWin.top;
                if (rcFill.left < rcFill.right) {
                    HDC hdc = GetWindowDC(hwnd);
                    if (hdc) {
                        HBRUSH hbr = CreateSolidBrush(RGB(255, 255, 255));
                        FillRect(hdc, &rcFill, hbr);
                        DeleteObject(hbr);
                        ReleaseDC(hwnd, hdc);
                    }
                }
            }
        }
        return lr;
    }

    // ── Background colours ────────────────────────────────────────────────────
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    // ── Minimum window size — enforced by Ne_LayoutToolbar's minClientW ───────
    case WM_GETMINMAXINFO: {
        NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (st && st->minClientW > 0) {
            // Convert minimum client width → minimum window width by adding
            // the non-client frame (borders + title bar + menu bar).
            RECT rc = { 0, 0, st->minClientW, 100 };
            AdjustWindowRectEx(&rc,
                (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE),
                GetMenu(hwnd) != NULL,
                (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            int minW = rc.right - rc.left;
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            if (minW > mmi->ptMinTrackSize.x)
                mmi->ptMinTrackSize.x = minW;
        }
        return 0;
    }

    // ── WM_CLOSE — prompt if modified ────────────────────────────────────────
    case WM_CLOSE:
        if (!Ne_CloseAllTabsForExit(hwnd)) return 0;
        DestroyWindow(hwnd);
        return 0;

    // ── WM_DESTROY ────────────────────────────────────────────────────────────
    case WM_DESTROY: {
        // Detach custom scrollbars before NeTabs_Destroy destroys the edit HWNDs
        Ne_DetachAllScrollbars();
        NeTabs_Destroy(hwnd);

        if (g_hMenuFont) { DeleteObject(g_hMenuFont); g_hMenuFont = NULL; }
        if (g_hFtpMenuIcon) { DestroyIcon(g_hFtpMenuIcon); g_hFtpMenuIcon = NULL; }
        for (auto* p : g_menuItemStorage) delete p;
        g_menuItemStorage.clear();

        auto freeProp = [&](const wchar_t* name) {
            HFONT h = (HFONT)GetPropW(hwnd, name);
            if (h) { DeleteObject(h); RemovePropW(hwnd, name); }
        };
        freeProp(L"neFont");
        freeProp(L"neFontBold");
        freeProp(L"neFontItalic");
        freeProp(L"neFontStrike");

        HideTooltip();
        CleanupTooltipSystem();

        NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (st) {
            if (st->hIconLarge) DestroyIcon(st->hIconLarge);
            if (st->hIconSmall) DestroyIcon(st->hIconSmall);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    SetProcessDPIAware();
    {
        HDC hdc = GetDC(NULL);
        g_dpiScale = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
        ReleaseDC(NULL, hdc);
    }

    Ne_LoadLocale();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc  = {};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = Ne_WndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NSBEditWnd";
    // Window class icon loaded inside WM_CREATE via WM_SETICON.
    RegisterClassExW(&wc);

    // Centre window on primary monitor.
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = S(960), wh = S(640);
    int wx = (sw - ww) / 2, wy = (sh - wh) / 2;

    std::wstring initTitle = Ls(L"UNTITLED") + std::wstring(L" \u2014 NSBEdit");
    s_hwndMain = CreateWindowExW(0, L"NSBEditWnd", initTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        wx, wy, ww, wh, NULL, NULL, hInst, NULL);

    if (!s_hwndMain) return 1;

    // If a file path was passed on the command line, open it.
    if (lpCmdLine && *lpCmdLine) {
        std::wstring arg = lpCmdLine;
        // Strip surrounding quotes if present.
        if (!arg.empty() && arg.front() == L'"') {
            arg.erase(0, 1);
            size_t q = arg.find(L'"');
            if (q != std::wstring::npos) arg.erase(q);
        }
        if (!arg.empty()) {
            Ne_LoadPathIntoEditor(s_hwndMain, arg);
        }
    }

    NeFtp_Init();
    NeCrypto_Init();
    NeProfiles_Init();
    // Load persistent zoom settings.
    NeProfiles_GetIntSetting("zoom_rtf", 100, g_zoomRtf);
    NeProfiles_GetIntSetting("zoom_sci",   0, g_zoomSci);
    // Clamp to valid ranges.
    if (g_zoomRtf < 10) g_zoomRtf = 10; if (g_zoomRtf > 500) g_zoomRtf = 500;
    if (g_zoomSci < -10) g_zoomSci = -10; if (g_zoomSci > 20) g_zoomSci = 20;

    ShowWindow(s_hwndMain, nCmdShow);
    UpdateWindow(s_hwndMain);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        // Route keyboard messages to the modeless Find/Replace dialog first.
        if (s_hwndFind && IsWindow(s_hwndFind) && IsDialogMessageW(s_hwndFind, &msg))
            continue;

        // Intercept Ctrl+N/O/S/Shift+S before the RichEdit consumes them.
        // Skip when Right-Alt (AltGr) is held — AltGr = Ctrl+Alt on Windows,
        // so keys like AltGr+0 (}) would otherwise be swallowed by the zoom handler.
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)
                && !(GetKeyState(VK_RMENU) & 0x8000)) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (msg.wParam == 'N') { SendMessageW(s_hwndMain, WM_COMMAND, IDM_NEW, 0);    continue; }
            if (msg.wParam == 'O') { SendMessageW(s_hwndMain, WM_COMMAND, IDM_OPEN, 0);   continue; }
            if (msg.wParam == 'S') {
                SendMessageW(s_hwndMain, WM_COMMAND, shift ? IDM_SAVEAS : IDM_SAVE, 0);
                continue;
            }
            if (msg.wParam == 'P') {
                SendMessageW(s_hwndMain, WM_COMMAND, shift ? IDM_EXPORT_PDF : IDM_PRINT, 0);
                continue;
            }
            if (msg.wParam == 'W') {
                if (NeTabs_GetCount(s_hwndMain) <= 1) {
                    SendMessageW(s_hwndMain, WM_CLOSE, 0, 0);
                } else {
                    int idx = NeTabs_GetActiveIndex(s_hwndMain);
                    if (idx >= 0) Ne_CloseTabAt(s_hwndMain, idx);
                }
                continue;
            }
            if (msg.wParam == VK_TAB) {
                if (NeTabs_Cycle(s_hwndMain, !shift)) {
                    HWND hAct = NeTabs_GetActiveEdit(s_hwndMain);
                    if (hAct) Ne_SyncToolbar(s_hwndMain, hAct);
                    Ne_UpdateStatusText(s_hwndMain);
                    Ne_UpdateTitle(s_hwndMain);
                }
                continue;
            }
            // Ctrl+B/I/U/F for text formatting / find (only when editor has focus).
            if (GetFocus() == NeTabs_GetActiveEdit(s_hwndMain)) {
                if (msg.wParam == 'B') { SendMessageW(s_hwndMain, WM_COMMAND, IDC_NE_BOLD,      0); continue; }
                if (msg.wParam == 'I') { SendMessageW(s_hwndMain, WM_COMMAND, IDC_NE_ITALIC,    0); continue; }
                if (msg.wParam == 'U') { SendMessageW(s_hwndMain, WM_COMMAND, IDC_NE_UNDERLINE, 0); continue; }
                if (msg.wParam == 'F') { SendMessageW(s_hwndMain, WM_COMMAND, IDC_NE_FIND,      0); continue; }
            }
            // Ctrl++ / Ctrl+- / Ctrl+0 — zoom in/out/reset (numpad and regular keys).
            if (msg.wParam == VK_ADD || msg.wParam == VK_OEM_PLUS) {
                Ne_StepZoom(s_hwndMain, +1); continue;
            }
            if (msg.wParam == VK_SUBTRACT || msg.wParam == VK_OEM_MINUS) {
                Ne_StepZoom(s_hwndMain, -1); continue;
            }
            if (msg.wParam == '0' || msg.wParam == VK_NUMPAD0) {
                Ne_StepZoom(s_hwndMain, 0); continue;
            }
        }
        // Ctrl+WheelScroll on an RTF tab: intercept, apply stepped zoom, save.
        if (msg.message == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(msg.wParam) & MK_CONTROL)) {
            NeTabDoc* wdoc = NeTabs_GetActiveDoc(s_hwndMain);
            if (wdoc && !wdoc->hSci && wdoc->hEdit) {
                int delta = GET_WHEEL_DELTA_WPARAM(msg.wParam);
                Ne_StepZoom(s_hwndMain, delta > 0 ? +1 : -1);
                continue; // don't pass to RichEdit's native zoom handler
            }
        }
        // F1 → Keyboard Shortcuts dialog
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F1) {
            SendMessageW(s_hwndMain, WM_COMMAND, IDM_SHORTCUTS, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    NeProfiles_Close();
    NeCrypto_Close();
    NeFtp_Cleanup();
    return (int)msg.wParam;
}
