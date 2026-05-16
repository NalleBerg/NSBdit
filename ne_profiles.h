#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

// FTP/SFTP connection profile.
struct NeProfile {
    int64_t      id              = 0;
    std::wstring friendlyName;
    std::wstring protocol;          // L"FTP" or L"SFTP"
    std::wstring host;
    int          port             = 21;
    std::wstring username;
    std::wstring password;          // decrypted in memory; empty if !rememberPassword
    bool         rememberPassword  = false;
    std::wstring initialPath;       // e.g. L"/"
};

// Call NeProfiles_Init() after NeCrypto_Init().
// DB is created at %APPDATA%\NSBEdit\nsbedit.db or .\nsbedit.db (portable).
bool NeProfiles_Init();
void NeProfiles_Close();

bool NeProfiles_Add   (NeProfile& p);           // sets p.id on success
bool NeProfiles_Update(const NeProfile& p);
bool NeProfiles_Delete(int64_t id);
bool NeProfiles_List  (std::vector<NeProfile>& out);
bool NeProfiles_GetById(int64_t id, NeProfile& out);

// Generic integer settings (key/value store in DB).
bool NeProfiles_GetIntSetting(const char* key, int defaultValue, int& out);
bool NeProfiles_SetIntSetting(const char* key, int value);
