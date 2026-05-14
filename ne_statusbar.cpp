#include "ne_statusbar.h"
#include "dpi.h"

#include <string>

#define NE_SB_CLASS L"NSBEditStatusBar"

struct NeStatusBarState {
    int  words    = 0;
    int  chars    = 0;
    bool modified = false;
    std::wstring wordsLabel   = L"Words";
    std::wstring charsLabel   = L"Chars";
    std::wstring savedLabel   = L"Saved";
    std::wstring unsavedLabel = L"Unsaved";
    std::wstring infoLabel;   // centre: encoding / file type
    HFONT hFont      = NULL;
    HICON hIconSaved   = NULL;
    HICON hIconUnsaved = NULL;
};

static HICON NeSB_LoadShell32Icon(int index)
{
    wchar_t path[MAX_PATH] = {};
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 16) return NULL;
    wcscat_s(path, L"\\shell32.dll");
    HICON hLarge = NULL, hSmall = NULL;
    PrivateExtractIconsW(path, index, 16, 16, &hSmall, NULL, 1, LR_DEFAULTCOLOR);
    return hSmall;
}

static LRESULT CALLBACK NeStatusBar_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NeStatusBarState* st = (NeStatusBarState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        st = new NeStatusBarState();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        ncm.lfMessageFont.lfHeight = -MulDiv(12, GetDpiForWindow(hwnd), 72);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        ncm.lfMessageFont.lfWeight  = FW_BOLD;
        st->hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        st->hIconSaved   = NeSB_LoadShell32Icon(294); // shell32 index 294 = green checkmark
        st->hIconUnsaved = NeSB_LoadShell32Icon(131); // shell32 index 131 = red X
        return 0;
    }
    case WM_DESTROY:
        if (st) {
            if (st->hFont)        DeleteObject(st->hFont);
            if (st->hIconSaved)   DestroyIcon(st->hIconSaved);
            if (st->hIconUnsaved) DestroyIcon(st->hIconUnsaved);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT: {
        if (!st) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc; GetClientRect(hwnd, &rc);

        // Background
        HBRUSH hbg = CreateSolidBrush(RGB(240, 240, 240));
        FillRect(hdc, &rc, hbg);
        DeleteObject(hbg);

        // Top border
        HPEN hpen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
        HPEN oldpen = (HPEN)SelectObject(hdc, hpen);
        MoveToEx(hdc, rc.left, rc.top, NULL);
        LineTo(hdc, rc.right, rc.top);
        SelectObject(hdc, oldpen);
        DeleteObject(hpen);

        HFONT oldFont = st->hFont ? (HFONT)SelectObject(hdc, st->hFont) : NULL;
        SetBkMode(hdc, TRANSPARENT);

        // Left side: Words: N    Chars: N
        wchar_t buf[128];
        swprintf_s(buf, L"%s: %d    %s: %d",
            st->wordsLabel.c_str(), st->words,
            st->charsLabel.c_str(), st->chars);
        RECT leftRc = { rc.left + S(10), rc.top + 1, rc.right / 3, rc.bottom };
        SetTextColor(hdc, RGB(60, 60, 60));
        DrawTextW(hdc, buf, -1, &leftRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Centre: encoding / file-type info
        if (!st->infoLabel.empty()) {
            RECT centreRc = { rc.right / 3, rc.top + 1, rc.right * 2 / 3, rc.bottom };
            SetTextColor(hdc, RGB(70, 80, 150));
            DrawTextW(hdc, st->infoLabel.c_str(), -1, &centreRc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Right side: icon + Saved / Unsaved
        const std::wstring& stateText = st->modified ? st->unsavedLabel : st->savedLabel;
        COLORREF stateColor = st->modified ? RGB(185, 90, 20) : RGB(40, 130, 40);
        HICON hStateIcon = st->modified ? st->hIconUnsaved : st->hIconSaved;

        // Measure text width to place icon+text as a unit pinned to the right
        int iconSz = S(14);
        int gap    = S(4);
        SIZE tsz = {};
        if (st->hFont) SelectObject(hdc, st->hFont);
        GetTextExtentPoint32W(hdc, stateText.c_str(), (int)stateText.size(), &tsz);
        int unitW   = (hStateIcon ? iconSz + gap : 0) + tsz.cx;
        int unitX   = rc.right - S(10) - unitW;
        int midY    = rc.top + (rc.bottom - rc.top) / 2;

        if (hStateIcon)
            DrawIconEx(hdc, unitX, midY - iconSz / 2, hStateIcon,
                       iconSz, iconSz, 0, NULL, DI_NORMAL);

        RECT rightRc = { unitX + (hStateIcon ? iconSz + gap : 0),
                         rc.top + 1, rc.right - S(10), rc.bottom };
        SetTextColor(hdc, stateColor);
        DrawTextW(hdc, stateText.c_str(), -1, &rightRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (oldFont) SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool NeStatusBar_RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = {};
    if (GetClassInfoExW(hInst, NE_SB_CLASS, &wc)) return true;
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = NeStatusBar_WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = NULL;
    wc.lpszClassName = NE_SB_CLASS;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    return RegisterClassExW(&wc) != 0;
}

HWND NeStatusBar_Create(HWND hwndParent, int id, HINSTANCE hInst)
{
    if (!NeStatusBar_RegisterClass(hInst)) return NULL;
    return CreateWindowExW(0, NE_SB_CLASS, L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, S(22),
        hwndParent, (HMENU)(UINT_PTR)id, hInst, NULL);
}

void NeStatusBar_Update(HWND hBar, int words, int chars, bool modified)
{
    if (!hBar) return;
    NeStatusBarState* st = (NeStatusBarState*)GetWindowLongPtrW(hBar, GWLP_USERDATA);
    if (!st) return;
    st->words    = words;
    st->chars    = chars;
    st->modified = modified;
    InvalidateRect(hBar, NULL, FALSE);
}

void NeStatusBar_SetModified(HWND hBar, bool modified)
{
    if (!hBar) return;
    NeStatusBarState* st = (NeStatusBarState*)GetWindowLongPtrW(hBar, GWLP_USERDATA);
    if (!st || st->modified == modified) return;
    st->modified = modified;
    InvalidateRect(hBar, NULL, FALSE);
}

int NeStatusBar_GetHeight(HWND hBar)
{
    if (!hBar) return S(22);
    RECT rc; GetClientRect(hBar, &rc);
    return rc.bottom > 0 ? rc.bottom : S(22);
}

void NeStatusBar_SetLabels(HWND hBar,
    const wchar_t* wordsLabel, const wchar_t* charsLabel,
    const wchar_t* savedLabel, const wchar_t* unsavedLabel)
{
    if (!hBar) return;
    NeStatusBarState* st = (NeStatusBarState*)GetWindowLongPtrW(hBar, GWLP_USERDATA);
    if (!st) return;
    if (wordsLabel)   st->wordsLabel   = wordsLabel;
    if (charsLabel)   st->charsLabel   = charsLabel;
    if (savedLabel)   st->savedLabel   = savedLabel;
    if (unsavedLabel) st->unsavedLabel = unsavedLabel;
    InvalidateRect(hBar, NULL, FALSE);
}

void NeStatusBar_SetInfo(HWND hBar, const wchar_t* info)
{
    if (!hBar) return;
    NeStatusBarState* st = (NeStatusBarState*)GetWindowLongPtrW(hBar, GWLP_USERDATA);
    if (!st) return;
    st->infoLabel = info ? info : L"";
    InvalidateRect(hBar, NULL, FALSE);
}
