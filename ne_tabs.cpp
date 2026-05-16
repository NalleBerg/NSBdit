#include "ne_tabs.h"

#include <windowsx.h>

#include <vector>
#include <algorithm>
#include <stdio.h>

#include "dpi.h"
#include "tooltip/tooltip.h"

struct NeTabsState {
    HWND hwndParent = NULL;
    HWND hTab = NULL;
    HWND hBtnNew = NULL;        // [+] new-tab button
    WNDPROC tabPrevProc = NULL;
    std::vector<NeTabDoc> docs;
    int activeIndex = -1;
    int tabCtrlId = 0;
    int editCtrlId = 0;
    int pad = 0;
    int tabHeight = 0;
    int tabX = 0;
    int tabY = 0;
    int tabW = 0;
    int tabH = 0;
    int editX = 0;
    int editY = 0;
    int editW = 0;
    int editH = 0;
    int hoverCloseIdx = -1;    // tab index whose × the mouse is currently over
    std::wstring richEditClass = L"RICHEDIT50W";
    std::wstring untitled = L"Untitled";
    std::wstring ctxNewTab  = L"New Tab";
    std::wstring ctxCloseTab = L"Close Tab";
};

static NeTabsState g_tabs;
static bool g_tipTracking = false;
static HWND g_tipTab = NULL;
static int g_tipIndex = -1;

// IDs used inside the tab context menu
#define NE_CTX_NEWTAB   1
#define NE_CTX_CLOSETAB 2

static std::wstring NeTabs_BaseName(const std::wstring& path, const std::wstring& untitled)
{
    if (path.empty()) return untitled;
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}

static std::wstring NeTabs_TitleFor(const NeTabDoc& d, const std::wstring& untitled)
{
    std::wstring t;
    if (d.modified) t += L"* ";
    t += NeTabs_BaseName(d.path, untitled);
    // Pad right so the owner-drawn × glyph has space (4 thin spaces)
    t += L"\u00a0\u00a0\u00a0\u00a0";
    return t;
}

static void NeTabs_ShowOnlyActive()
{
    for (int i = 0; i < (int)g_tabs.docs.size(); ++i) {
        bool active = (i == g_tabs.activeIndex);
        auto& d = g_tabs.docs[i];
        if (d.hSci) {
            ShowWindow(d.hSci,  active ? SW_SHOW : SW_HIDE);
            ShowWindow(d.hEdit, SW_HIDE);
        } else {
            ShowWindow(d.hEdit, active ? SW_SHOW : SW_HIDE);
        }
    }
}

static void NeTabs_ShowHoverTip(int idx)
{
    if (idx < 0 || idx >= (int)g_tabs.docs.size()) {
        HideTooltip();
        g_tipIndex = -1;
        return;
    }
    if (g_tipIndex == idx && IsTooltipVisible()) return;

    RECT ir = {};
    if (!TabCtrl_GetItemRect(g_tabs.hTab, idx, &ir)) return;
    POINT p = { ir.left, ir.bottom + S(2) };
    ClientToScreen(g_tabs.hTab, &p);

    std::wstring txt;
    const NeTabDoc& doc = g_tabs.docs[idx];
    if (doc.isFtpFile && !doc.ftpRemotePath.empty()) {
        txt = doc.ftpFriendlyName + L" \u2014 " + doc.ftpRemotePath;
    } else {
        txt = doc.path.empty() ? g_tabs.untitled : doc.path;
    }
    std::vector<TooltipEntry> entries = { {L"", txt.c_str()} };
    ShowMultilingualTooltip(entries, p.x, p.y, g_tabs.hwndParent);
    g_tipIndex = idx;
}

static int NeTabs_HitCloseIndex(POINT ptClientOnTab)
{
    TCHITTESTINFO hti = {};
    hti.pt = ptClientOnTab;
    int idx = TabCtrl_HitTest(g_tabs.hTab, &hti);
    if (idx < 0) return -1;

    RECT ir = {};
    if (!TabCtrl_GetItemRect(g_tabs.hTab, idx, &ir)) return -1;
    int closeW = S(16);
    if (ptClientOnTab.x >= ir.right - closeW && ptClientOnTab.x <= ir.right) return idx;
    return -1;
}

// Returns the bounding rect of the × button for tab idx (in tab-ctrl client coords).
static RECT NeTabs_CloseRect(int idx)
{
    RECT ir = {};
    if (!TabCtrl_GetItemRect(g_tabs.hTab, idx, &ir)) return ir;
    int sz = S(14);
    int cx = ir.right - S(2) - sz;
    int cy = ir.top + (ir.bottom - ir.top - sz) / 2;
    RECT cr = { cx, cy, cx + sz, cy + sz };
    return cr;
}

// Draws the × glyphs over all tabs. Call after default WM_PAINT.
static void NeTabs_DrawCloseGlyphs(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);
    if (!hdc) return;

    int n = (int)g_tabs.docs.size();
    for (int i = 0; i < n; ++i) {
        RECT cr = NeTabs_CloseRect(i);
        bool hover = (i == g_tabs.hoverCloseIdx);

        // Draw small circle background on hover
        if (hover) {
            HBRUSH hbg = CreateSolidBrush(RGB(210, 90, 90));
            HBRUSH old = (HBRUSH)SelectObject(hdc, hbg);
            HPEN np = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
            Ellipse(hdc, cr.left, cr.top, cr.right, cr.bottom);
            SelectObject(hdc, old);
            SelectObject(hdc, np);
            DeleteObject(hbg);
        }

        // Draw × glyph
        COLORREF col = hover ? RGB(255, 255, 255) : RGB(100, 100, 100);
        HPEN hp = CreatePen(PS_SOLID, S(1) + 1, col);
        HPEN oldpen = (HPEN)SelectObject(hdc, hp);
        int m = S(3);
        MoveToEx(hdc, cr.left + m,     cr.top + m,      NULL);
        LineTo  (hdc, cr.right - m - 1, cr.bottom - m - 1);
        MoveToEx(hdc, cr.right - m - 1, cr.top + m,      NULL);
        LineTo  (hdc, cr.left + m,     cr.bottom - m - 1);
        SelectObject(hdc, oldpen);
        DeleteObject(hp);
    }

    ReleaseDC(hwnd, hdc);
}

static LRESULT CALLBACK NeTabs_TabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT: {
        // Let the system draw the tabs first, then overlay × glyphs.
        LRESULT r = CallWindowProcW(g_tabs.tabPrevProc, hwnd, msg, wParam, lParam);
        NeTabs_DrawCloseGlyphs(hwnd);
        return r;
    }
    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Hover-close tracking: check if cursor is in a × rect
        int newHover = -1;
        int n = (int)g_tabs.docs.size();
        for (int i = 0; i < n; ++i) {
            RECT cr = NeTabs_CloseRect(i);
            if (p.x >= cr.left && p.x <= cr.right && p.y >= cr.top && p.y <= cr.bottom) {
                newHover = i;
                break;
            }
        }
        if (newHover != g_tabs.hoverCloseIdx) {
            g_tabs.hoverCloseIdx = newHover;
            InvalidateRect(hwnd, NULL, FALSE);
        }

        // Tooltip for full path
        TCHITTESTINFO hti = {};
        hti.pt = p;
        int idx = TabCtrl_HitTest(hwnd, &hti);
        NeTabs_ShowHoverTip(idx);

        if (!g_tipTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_tipTracking = true;
            g_tipTab = hwnd;
        }
        break;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        g_tipTracking = false;
        g_tipTab = NULL;
        g_tipIndex = -1;
        if (g_tabs.hoverCloseIdx != -1) {
            g_tabs.hoverCloseIdx = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_LBUTTONUP: {
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int idx = NeTabs_HitCloseIndex(p);
        if (idx >= 0 && g_tabs.hwndParent) {
            PostMessageW(g_tabs.hwndParent, NE_WM_TABCLOSE, (WPARAM)idx, 0);
            return 0;
        }
        break;
    }
    case WM_RBUTTONUP: {
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        // Hit-test to see if we're over a real tab
        TCHITTESTINFO hti = {};
        hti.pt = p;
        int tabIdx = TabCtrl_HitTest(hwnd, &hti);

        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, NE_CTX_NEWTAB, g_tabs.ctxNewTab.c_str());
        if (tabIdx >= 0)
            AppendMenuW(hMenu, MF_STRING, NE_CTX_CLOSETAB, g_tabs.ctxCloseTab.c_str());

        POINT screen = p;
        ClientToScreen(hwnd, &screen);
        int cmd = (int)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      screen.x, screen.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);

        if (cmd == NE_CTX_NEWTAB && g_tabs.hwndParent)
            PostMessageW(g_tabs.hwndParent, NE_WM_TABNEW, 0, 0);
        else if (cmd == NE_CTX_CLOSETAB && tabIdx >= 0 && g_tabs.hwndParent)
            PostMessageW(g_tabs.hwndParent, NE_WM_TABCLOSE, (WPARAM)tabIdx, 0);
        return 0;
    }
    }
    return CallWindowProcW(g_tabs.tabPrevProc, hwnd, msg, wParam, lParam);
}

static bool NeTabs_CreateEdit(NeTabDoc& d)
{
    d.hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, g_tabs.richEditClass.c_str(), L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
        g_tabs.editX, g_tabs.editY, std::max(1, g_tabs.editW), std::max(1, g_tabs.editH),
        g_tabs.hwndParent, (HMENU)(UINT_PTR)g_tabs.editCtrlId, GetModuleHandleW(NULL), NULL);
    return d.hEdit != NULL;
}

bool NeTabs_Create(const NeTabsCreateParams& p)
{
    g_tabs = {};
    g_tabs.hwndParent = p.hwndParent;
    g_tabs.tabCtrlId = p.tabCtrlId;
    g_tabs.editCtrlId = p.editCtrlId;
    g_tabs.pad = p.pad;
    g_tabs.tabHeight = p.tabHeight;
    g_tabs.untitled = p.untitledLabel ? p.untitledLabel : L"Untitled";
    g_tabs.richEditClass = p.richEditClass ? p.richEditClass : L"RICHEDIT50W";

    g_tabs.hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        g_tabs.pad, g_tabs.pad, S(400), g_tabs.tabHeight,
        g_tabs.hwndParent, (HMENU)(UINT_PTR)g_tabs.tabCtrlId, p.hInst, NULL);
    if (!g_tabs.hTab) return false;

    // [+] new-tab button sits to the right of the tab strip
    int btnW = g_tabs.tabHeight;
    g_tabs.hBtnNew = CreateWindowExW(0, L"BUTTON", L"+",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, btnW, g_tabs.tabHeight,
        g_tabs.hwndParent, (HMENU)(UINT_PTR)(g_tabs.tabCtrlId + 10), p.hInst, NULL);

    g_tabs.tabPrevProc = (WNDPROC)SetWindowLongPtrW(g_tabs.hTab, GWLP_WNDPROC, (LONG_PTR)NeTabs_TabProc);

    NeTabDoc first = {};
    if (!NeTabs_CreateEdit(first)) return false;
    g_tabs.docs.push_back(first);
    g_tabs.activeIndex = 0;

    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    std::wstring t = NeTabs_TitleFor(g_tabs.docs[0], g_tabs.untitled);
    ti.pszText = (LPWSTR)t.c_str();
    TabCtrl_InsertItem(g_tabs.hTab, 0, &ti);
    TabCtrl_SetCurSel(g_tabs.hTab, 0);
    NeTabs_ShowOnlyActive();
    return true;
}

void NeTabs_Destroy(HWND hwndParent)
{
    if (g_tabs.hwndParent != hwndParent) return;
    HideTooltip();
    for (auto& d : g_tabs.docs) {
        if (d.hEdit && IsWindow(d.hEdit)) DestroyWindow(d.hEdit);
        if (d.hSci  && IsWindow(d.hSci))  DestroyWindow(d.hSci);
        d.hEdit = NULL;
        d.hSci  = NULL;
    }
    g_tabs.docs.clear();
    if (g_tabs.hBtnNew && IsWindow(g_tabs.hBtnNew)) DestroyWindow(g_tabs.hBtnNew);
    if (g_tabs.hTab && IsWindow(g_tabs.hTab)) {
        if (g_tabs.tabPrevProc)
            SetWindowLongPtrW(g_tabs.hTab, GWLP_WNDPROC, (LONG_PTR)g_tabs.tabPrevProc);
        DestroyWindow(g_tabs.hTab);
    }
    g_tabs = {};
}

HWND NeTabs_GetTabHwnd(HWND hwndParent)
{
    return (g_tabs.hwndParent == hwndParent) ? g_tabs.hTab : NULL;
}

int NeTabs_GetCount(HWND hwndParent)
{
    return (g_tabs.hwndParent == hwndParent) ? (int)g_tabs.docs.size() : 0;
}

int NeTabs_GetActiveIndex(HWND hwndParent)
{
    return (g_tabs.hwndParent == hwndParent) ? g_tabs.activeIndex : -1;
}

bool NeTabs_SetActive(HWND hwndParent, int index)
{
    if (g_tabs.hwndParent != hwndParent) return false;
    if (index < 0 || index >= (int)g_tabs.docs.size()) return false;
    g_tabs.activeIndex = index;
    TabCtrl_SetCurSel(g_tabs.hTab, index);
    NeTabs_ShowOnlyActive();
    HWND focusTarget = g_tabs.docs[index].hSci ? g_tabs.docs[index].hSci : g_tabs.docs[index].hEdit;
    SetFocus(focusTarget);
    return true;
}

bool NeTabs_Cycle(HWND hwndParent, bool forward)
{
    if (g_tabs.hwndParent != hwndParent) return false;
    int n = (int)g_tabs.docs.size();
    if (n <= 1) return false;
    int idx = g_tabs.activeIndex;
    idx = forward ? (idx + 1) % n : (idx + n - 1) % n;
    return NeTabs_SetActive(hwndParent, idx);
}

NeTabDoc* NeTabs_GetActiveDoc(HWND hwndParent)
{
    if (g_tabs.hwndParent != hwndParent) return NULL;
    if (g_tabs.activeIndex < 0 || g_tabs.activeIndex >= (int)g_tabs.docs.size()) return NULL;
    return &g_tabs.docs[g_tabs.activeIndex];
}

NeTabDoc* NeTabs_GetDocByIndex(HWND hwndParent, int index)
{
    if (g_tabs.hwndParent != hwndParent) return NULL;
    if (index < 0 || index >= (int)g_tabs.docs.size()) return NULL;
    return &g_tabs.docs[index];
}

NeTabDoc* NeTabs_GetDocByEdit(HWND hwndParent, HWND hEdit)
{
    if (g_tabs.hwndParent != hwndParent) return NULL;
    for (auto& d : g_tabs.docs) {
        if (d.hEdit == hEdit) return &d;
    }
    return NULL;
}

HWND NeTabs_GetActiveEdit(HWND hwndParent)
{
    NeTabDoc* d = NeTabs_GetActiveDoc(hwndParent);
    return d ? d->hEdit : NULL;
}

bool NeTabs_AddUntitled(HWND hwndParent)
{
    if (g_tabs.hwndParent != hwndParent) return false;

    NeTabDoc d = {};
    if (!NeTabs_CreateEdit(d)) return false;
    g_tabs.docs.push_back(d);

    int idx = (int)g_tabs.docs.size() - 1;
    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    std::wstring t = NeTabs_TitleFor(g_tabs.docs[idx], g_tabs.untitled);
    ti.pszText = (LPWSTR)t.c_str();
    TabCtrl_InsertItem(g_tabs.hTab, idx, &ti);

    return NeTabs_SetActive(hwndParent, idx);
}

bool NeTabs_CloseTab(HWND hwndParent, int index)
{
    if (g_tabs.hwndParent != hwndParent) return false;
    if (index < 0 || index >= (int)g_tabs.docs.size()) return false;

    if (g_tabs.docs[index].hEdit && IsWindow(g_tabs.docs[index].hEdit))
        DestroyWindow(g_tabs.docs[index].hEdit);
    if (g_tabs.docs[index].hSci && IsWindow(g_tabs.docs[index].hSci))
        DestroyWindow(g_tabs.docs[index].hSci);

    g_tabs.docs.erase(g_tabs.docs.begin() + index);
    TabCtrl_DeleteItem(g_tabs.hTab, index);

    if (g_tabs.docs.empty()) {
        return NeTabs_AddUntitled(hwndParent);
    }

    if (g_tabs.activeIndex >= (int)g_tabs.docs.size())
        g_tabs.activeIndex = (int)g_tabs.docs.size() - 1;
    if (index <= g_tabs.activeIndex && g_tabs.activeIndex > 0)
        g_tabs.activeIndex--;

    TabCtrl_SetCurSel(g_tabs.hTab, g_tabs.activeIndex);
    NeTabs_ShowOnlyActive();
    SetFocus(g_tabs.docs[g_tabs.activeIndex].hEdit);
    return true;
}

void NeTabs_SetUntitledLabel(HWND hwndParent, const wchar_t* untitledLabel)
{
    if (g_tabs.hwndParent != hwndParent) return;
    g_tabs.untitled = untitledLabel ? untitledLabel : L"Untitled";
    NeTabs_UpdateAllTitles(hwndParent);
}

void NeTabs_SetContextLabels(HWND hwndParent, const wchar_t* newTab, const wchar_t* closeTab)
{
    if (g_tabs.hwndParent != hwndParent) return;
    if (newTab)   g_tabs.ctxNewTab   = newTab;
    if (closeTab) g_tabs.ctxCloseTab = closeTab;
}

void NeTabs_UpdateTabTitle(HWND hwndParent, int index)
{
    if (g_tabs.hwndParent != hwndParent) return;
    if (index < 0 || index >= (int)g_tabs.docs.size()) return;

    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    std::wstring t = NeTabs_TitleFor(g_tabs.docs[index], g_tabs.untitled);
    ti.pszText = (LPWSTR)t.c_str();
    TabCtrl_SetItem(g_tabs.hTab, index, &ti);
}

void NeTabs_UpdateAllTitles(HWND hwndParent)
{
    if (g_tabs.hwndParent != hwndParent) return;
    for (int i = 0; i < (int)g_tabs.docs.size(); ++i)
        NeTabs_UpdateTabTitle(hwndParent, i);
}

void NeTabs_GetTabRowRect(HWND hwndParent, int* outY, int* outH)
{
    if (outY) *outY = g_tabs.tabY;
    if (outH) *outH = g_tabs.tabH;
}

void NeTabs_ShowTabContextMenu(HWND hwndParent, int screenX, int screenY, int tabIndex)
{
    if (g_tabs.hwndParent != hwndParent) return;

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, NE_CTX_NEWTAB, g_tabs.ctxNewTab.c_str());
    if (tabIndex >= 0)
        AppendMenuW(hMenu, MF_STRING, NE_CTX_CLOSETAB, g_tabs.ctxCloseTab.c_str());

    int cmd = (int)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  screenX, screenY, 0,
                                  g_tabs.hTab ? g_tabs.hTab : hwndParent, NULL);
    DestroyMenu(hMenu);

    if (cmd == NE_CTX_NEWTAB)
        PostMessageW(hwndParent, NE_WM_TABNEW, 0, 0);
    else if (cmd == NE_CTX_CLOSETAB && tabIndex >= 0)
        PostMessageW(hwndParent, NE_WM_TABCLOSE, (WPARAM)tabIndex, 0);
}

void NeTabs_SetRects(HWND hwndParent,
    int tabX, int tabY, int tabW, int tabH,
    int editX, int editY, int editW, int editH)
{
    if (g_tabs.hwndParent != hwndParent) return;

    g_tabs.tabX = tabX; g_tabs.tabY = tabY; g_tabs.tabW = tabW; g_tabs.tabH = tabH;
    g_tabs.editX = editX; g_tabs.editY = editY; g_tabs.editW = editW; g_tabs.editH = editH;

    // Reserve space for [+] button on the right of the tab strip
    int btnW = tabH;
    int tabCtrlW = std::max(1, tabW - btnW - S(2));

    if (g_tabs.hTab)
        SetWindowPos(g_tabs.hTab, NULL, tabX, tabY, tabCtrlW, std::max(1, tabH), SWP_NOZORDER | SWP_NOACTIVATE);

    if (g_tabs.hBtnNew)
        SetWindowPos(g_tabs.hBtnNew, NULL, tabX + tabCtrlW + S(2), tabY,
                     btnW, std::max(1, tabH), SWP_NOZORDER | SWP_NOACTIVATE);

    for (auto& d : g_tabs.docs) {
        if (d.hEdit)
            SetWindowPos(d.hEdit, NULL, editX, editY, std::max(1, editW), std::max(1, editH), SWP_NOZORDER | SWP_NOACTIVATE);
        if (d.hSci)
            SetWindowPos(d.hSci,  NULL, editX, editY, std::max(1, editW), std::max(1, editH), SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

HWND NeTabs_GetActiveScintilla(HWND hwndParent)
{
    NeTabDoc* d = NeTabs_GetActiveDoc(hwndParent);
    return d ? d->hSci : NULL;
}
