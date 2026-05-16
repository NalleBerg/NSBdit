#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include "ne_profiles.h"

// A single entry returned by NeFtp_ListDir.
struct NeFtpEntry {
    std::wstring name;
    bool         isDir       = false;
    int64_t      size        = 0;
    int          permissions = -1;      // Unix permission bits, -1 if unknown
    std::wstring permStr;               // e.g. "rwxr-xr-x" (9 chars, no type bit)
};

// Initialise / tear down the libcurl global state.
void NeFtp_Init();
void NeFtp_Cleanup();

// Connect by verifying credentials (lists the initial path).
// If the profile is already connected, it is made active and true is returned.
bool NeFtp_Connect(const NeProfile& p);
// Disconnect the currently-active connection.
void NeFtp_Disconnect();
// Disconnect a specific profile by id.
void NeFtp_Disconnect(int64_t profileId);
// Disconnect all open connections (call from WM_DESTROY via NeFtp_Cleanup).
void NeFtp_DisconnectAll();
// True if the currently-active connection is alive.
bool NeFtp_IsConnected();
// True if the specific profile is connected.
bool NeFtp_IsConnected(int64_t profileId);
// Switch which connection is active; returns false if that profile is not connected.
bool NeFtp_SetActiveConn(int64_t profileId);
const NeProfile& NeFtp_GetActiveProfile();

// Directory listing.  For FTP tries MLSD first, falls back to LIST.
bool NeFtp_ListDir(const std::wstring& remotePath, std::vector<NeFtpEntry>& out);

// Download remotePath to a temp file; outLocalPath receives the local path.
bool NeFtp_DownloadToTemp(const std::wstring& remotePath, std::wstring& outLocalPath);

// Upload localPath to remotePath on the active server.
bool NeFtp_Upload(const std::wstring& localPath, const std::wstring& remotePath);

// Set Unix permissions.  FTP uses "SITE CHMOD", SFTP uses "chmod".
bool NeFtp_Chmod(const std::wstring& remotePath, int mode);
bool NeFtp_Delete(const std::wstring& remotePath, bool isDir);
bool NeFtp_MkDir(const std::wstring& remotePath);
bool NeFtp_CreateEmptyFile(const std::wstring& remotePath);

// Human-readable description of the last error.
std::wstring NeFtp_GetLastError();
