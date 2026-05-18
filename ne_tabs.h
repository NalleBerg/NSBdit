#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <cstdint>

// Parent-notification messages posted by the tab module.
#define NE_WM_TABCLOSE (WM_APP + 201)   // wParam = tab index to close
#define NE_WM_TABNEW   (WM_APP + 202)   // new tab requested (+ button / context menu)

struct NeTabDoc {
    HWND hEdit = NULL;
    std::wstring path;
    bool modified = false;
    bool suppressChange = false;
    bool hasDiskStamp = false;
    FILETIME diskWriteTime = {};
    ULONGLONG diskFileSize = 0;
    int encoding = 0; // NeEncoding cast to int; 0 = Unknown
    std::wstring prevPlainPath; // set when converting plain→RTF; cleared after save
    HWND hSci = NULL;       // Scintilla editor (NULL = use hEdit/RichEdit)
    HWND hLineGutter = NULL;// companion line-number panel for the RichEdit side
    int  langId = -1;       // index into s_langs[] in NSBEdit.cpp
    bool langUserSet = false; // user manually overrode auto-detect
    bool lineNumsVisible = true; // whether the line-number margin is shown
    // ── FTP/SFTP remote editing ───────────────────────────────────────────────
    bool         isFtpFile          = false; // file was opened from an FTP/SFTP server
    int64_t      ftpProfileId       = -1;    // NeProfile::id of the active server
    std::wstring ftpRemotePath;              // absolute path on the server
    std::wstring ftpFriendlyName;            // display name of the server (for tooltip)
};

struct NeTabsCreateParams {
    HWND hwndParent = NULL;
    HINSTANCE hInst = NULL;
    const wchar_t* richEditClass = L"RICHEDIT50W";
    int tabCtrlId = 0;
    int editCtrlId = 0;
    int pad = 0;
    int tabHeight = 0;
    const wchar_t* untitledLabel = L"Untitled";
    void (*pfnEditCreated)(HWND hEdit) = nullptr; // called whenever a new RichEdit is created
};

bool NeTabs_Create(const NeTabsCreateParams& p);
void NeTabs_Destroy(HWND hwndParent);

HWND NeTabs_GetTabHwnd(HWND hwndParent);
int NeTabs_GetCount(HWND hwndParent);
int NeTabs_GetActiveIndex(HWND hwndParent);
bool NeTabs_SetActive(HWND hwndParent, int index);
bool NeTabs_Cycle(HWND hwndParent, bool forward);

NeTabDoc* NeTabs_GetActiveDoc(HWND hwndParent);
NeTabDoc* NeTabs_GetDocByIndex(HWND hwndParent, int index);
NeTabDoc* NeTabs_GetDocByEdit(HWND hwndParent, HWND hEdit);
HWND NeTabs_GetActiveEdit(HWND hwndParent);

bool NeTabs_AddUntitled(HWND hwndParent);
bool NeTabs_CloseTab(HWND hwndParent, int index);

void NeTabs_SetUntitledLabel(HWND hwndParent, const wchar_t* untitledLabel);
void NeTabs_SetContextLabels(HWND hwndParent, const wchar_t* newTab, const wchar_t* closeTab);

// Show the tab right-click context menu at the given screen coords.
// tabIndex: the tab under the cursor, or -1 if over empty space.
// When tabIndex < 0 only "New Tab" is shown.
void NeTabs_ShowTabContextMenu(HWND hwndParent, int screenX, int screenY, int tabIndex);

// Returns the Y range of the tab strip in parent client coords.
void NeTabs_GetTabRowRect(HWND hwndParent, int* outY, int* outH);

void NeTabs_UpdateTabTitle(HWND hwndParent, int index);
void NeTabs_UpdateAllTitles(HWND hwndParent);

void NeTabs_SetRects(HWND hwndParent,
    int tabX, int tabY, int tabW, int tabH,
    int editX, int editY, int editW, int editH);

HWND NeTabs_GetActiveScintilla(HWND hwndParent);

// Scroll the tab strip left (forward=false) or right (forward=true).

