#pragma once
#include <windows.h>

// Creates a custom owner-drawn status bar as a child of hwndParent.
// Height is fixed at S(22) dpi-scaled pixels.
HWND NeStatusBar_Create(HWND hwndParent, int id, HINSTANCE hInst);

// Updates word count, char count, and modified (save) state all at once.
void NeStatusBar_Update(HWND hBar, int words, int chars, bool modified);

// Lightweight update of the save state only (no count recompute needed).
void NeStatusBar_SetModified(HWND hBar, bool modified);

// Returns the bar's fixed height in pixels (S(22)).
int NeStatusBar_GetHeight(HWND hBar);

// Set the localized label strings displayed in the bar.
//   wordsLabel   e.g. L"Words"
//   charsLabel   e.g. L"Chars"
//   savedLabel   e.g. L"Saved"
//   unsavedLabel e.g. L"Unsaved"
void NeStatusBar_SetLabels(HWND hBar,
    const wchar_t* wordsLabel, const wchar_t* charsLabel,
    const wchar_t* savedLabel, const wchar_t* unsavedLabel);

// Set the centre info string (encoding / file type). Pass NULL to clear.
void NeStatusBar_SetInfo(HWND hBar, const wchar_t* info);
