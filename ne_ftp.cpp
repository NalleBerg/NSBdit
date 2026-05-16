#include "ne_ftp.h"
#include "curl/include/curl/curl.h"
#include <shlobj.h>
#include <stdio.h>
#include <sstream>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

// ── Connection pool ───────────────────────────────────────────────────────────
struct NeFtpConn { CURL* curl; NeProfile profile; };
static std::map<int64_t, NeFtpConn> s_conns;
static int64_t   s_activeId = 0;

// These point at the currently-active connection so all operation
// functions below continue to work without modification.
static CURL*        s_curl      = nullptr;
static NeProfile    s_activeProfile;
static std::wstring s_lastError;
static char         s_errbuf[CURL_ERROR_SIZE] = {};

// ── String helpers ────────────────────────────────────────────────────────────
static std::string Nf_W2U(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, NULL, NULL);
    return s;
}

static std::wstring Nf_U2W(const char* u)
{
    if (!u || !*u) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, NULL, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u, -1, w.data(), n);
    return w;
}

// ── curl callbacks ────────────────────────────────────────────────────────────
static size_t Nf_WriteBuf(void* ptr, size_t sz, size_t nm, void* user)
{
    std::string* buf = (std::string*)user;
    buf->append((char*)ptr, sz * nm);
    return sz * nm;
}

static size_t Nf_ReadFile(void* ptr, size_t sz, size_t nm, void* user)
{
    return fread(ptr, sz, nm, (FILE*)user);
}

static size_t Nf_WriteFile(void* ptr, size_t sz, size_t nm, void* user)
{
    return fwrite(ptr, sz, nm, (FILE*)user);
}

// ── URL builder ───────────────────────────────────────────────────────────────
static std::string Nf_BuildUrl(const std::wstring& path)
{
    std::string proto = Nf_W2U(s_activeProfile.protocol);
    std::transform(proto.begin(), proto.end(), proto.begin(),
                   [](unsigned char c){ return (char)tolower(c); });
    std::string url = proto + "://"
                    + Nf_W2U(s_activeProfile.host)
                    + ":" + std::to_string(s_activeProfile.port);
    std::string ps = Nf_W2U(path);
    if (ps.empty() || ps[0] != '/') url += '/';
    url += ps;
    return url;
}

// ── Apply global options to s_curl ───────────────────────────────────────────
// Called once in Connect; individual operations update only URL / mode opts.
static void Nf_ApplyCommonOpts()
{
    curl_easy_setopt(s_curl, CURLOPT_USERNAME, Nf_W2U(s_activeProfile.username).c_str());
    curl_easy_setopt(s_curl, CURLOPT_PASSWORD, Nf_W2U(s_activeProfile.password).c_str());
    curl_easy_setopt(s_curl, CURLOPT_ERRORBUFFER, s_errbuf);
    curl_easy_setopt(s_curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(s_curl, CURLOPT_LOW_SPEED_LIMIT, 0L);
    curl_easy_setopt(s_curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(s_curl, CURLOPT_VERBOSE, 0L);
    if (s_activeProfile.protocol == L"SFTP") {
        curl_easy_setopt(s_curl, CURLOPT_SSH_AUTH_TYPES, (long)CURLSSH_AUTH_PASSWORD);
        // Accept any host key (TOFU-less for user convenience; acceptable in
        // a desktop client for web developers on trusted networks).
        curl_easy_setopt(s_curl, CURLOPT_SSH_KNOWNHOSTS, (char*)NULL);
    } else {
        curl_easy_setopt(s_curl, CURLOPT_FTP_SKIP_PASV_IP, 1L);
        curl_easy_setopt(s_curl, CURLOPT_FTP_USE_EPRT, 0L);
    }
}

// Reset per-operation options back to safe defaults before each operation.
static void Nf_ResetOpOpts(std::string& buf)
{
    s_errbuf[0] = 0;
    buf.clear();
    curl_easy_setopt(s_curl, CURLOPT_NOBODY,          0L);
    curl_easy_setopt(s_curl, CURLOPT_UPLOAD,          0L);
    curl_easy_setopt(s_curl, CURLOPT_CUSTOMREQUEST,   (char*)NULL);
    curl_easy_setopt(s_curl, CURLOPT_QUOTE,           (struct curl_slist*)NULL);
    curl_easy_setopt(s_curl, CURLOPT_POSTQUOTE,       (struct curl_slist*)NULL);
    curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION,   Nf_WriteBuf);
    curl_easy_setopt(s_curl, CURLOPT_WRITEDATA,       &buf);
    curl_easy_setopt(s_curl, CURLOPT_READFUNCTION,    (curl_read_callback)NULL);
    curl_easy_setopt(s_curl, CURLOPT_READDATA,        (void*)NULL);
    curl_easy_setopt(s_curl, CURLOPT_INFILESIZE_LARGE,(curl_off_t)-1);
}

static void Nf_SetLastError(CURLcode rc)
{
    s_lastError = s_errbuf[0]
                ? Nf_U2W(s_errbuf)
                : Nf_U2W(curl_easy_strerror(rc));
}

// ── Directory-listing parsers ─────────────────────────────────────────────────

// Parse a permissions string like "rwxr-xr-x" (9 chars, no leading type byte)
// or "-rwxr-xr-x" (10 chars including type byte).
static int Nf_ParsePermStr(const std::string& s, std::wstring& outStr)
{
    const char* p = s.c_str();
    // Skip leading type byte if present (d, -, l, etc.)
    if (s.size() >= 10 && (p[0] == '-' || p[0] == 'd' || p[0] == 'l' ||
                           p[0] == 'b' || p[0] == 'c' || p[0] == 'p' ||
                           p[0] == 's')) {
        ++p;
    }
    if (strlen(p) < 9) { outStr = Nf_U2W(p); return -1; }

    int mode = 0;
    if (p[0] == 'r') mode |= 0400;
    if (p[1] == 'w') mode |= 0200;
    if (p[2] == 'x' || p[2] == 's') mode |= 0100;
    if (p[3] == 'r') mode |= 0040;
    if (p[4] == 'w') mode |= 0020;
    if (p[5] == 'x' || p[5] == 's') mode |= 0010;
    if (p[6] == 'r') mode |= 0004;
    if (p[7] == 'w') mode |= 0002;
    if (p[8] == 'x' || p[8] == 't') mode |= 0001;

    char buf[10];
    buf[0] = (mode & 0400) ? 'r' : '-';
    buf[1] = (mode & 0200) ? 'w' : '-';
    buf[2] = (mode & 0100) ? 'x' : '-';
    buf[3] = (mode & 0040) ? 'r' : '-';
    buf[4] = (mode & 0020) ? 'w' : '-';
    buf[5] = (mode & 0010) ? 'x' : '-';
    buf[6] = (mode & 0004) ? 'r' : '-';
    buf[7] = (mode & 0002) ? 'w' : '-';
    buf[8] = (mode & 0001) ? 'x' : '-';
    buf[9] = 0;
    outStr = Nf_U2W(buf);
    return mode;
}

// MLSD format: "fact=val;fact=val; name\r\n"
static void Nf_ParseMlsd(const std::string& data, std::vector<NeFtpEntry>& out)
{
    std::istringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string facts = line.substr(0, sp);
        std::string name  = line.substr(sp + 1);
        if (name == "." || name == "..") continue;

        NeFtpEntry e;
        e.name        = Nf_U2W(name.c_str());
        e.permissions = -1;

        std::istringstream fs(facts);
        std::string fact;
        while (std::getline(fs, fact, ';')) {
            if (fact.empty()) continue;
            size_t eq = fact.find('=');
            if (eq == std::string::npos) continue;
            std::string key = fact.substr(0, eq);
            std::string val = fact.substr(eq + 1);
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c){ return (char)tolower(c); });
            if (key == "type") {
                std::string vl = val;
                std::transform(vl.begin(), vl.end(), vl.begin(),
                               [](unsigned char c){ return (char)tolower(c); });
                e.isDir = (vl == "dir" || vl == "cdir" || vl == "pdir");
            } else if (key == "size") {
                e.size = (int64_t)_atoi64(val.c_str());
            } else if (key == "unix.mode") {
                // Octal string, e.g. "0755"
                e.permissions = (int)strtol(val.c_str(), NULL, 8);
                char b[10] = {};
                b[0] = (e.permissions & 0400) ? 'r' : '-';
                b[1] = (e.permissions & 0200) ? 'w' : '-';
                b[2] = (e.permissions & 0100) ? 'x' : '-';
                b[3] = (e.permissions & 0040) ? 'r' : '-';
                b[4] = (e.permissions & 0020) ? 'w' : '-';
                b[5] = (e.permissions & 0010) ? 'x' : '-';
                b[6] = (e.permissions & 0004) ? 'r' : '-';
                b[7] = (e.permissions & 0002) ? 'w' : '-';
                b[8] = (e.permissions & 0001) ? 'x' : '-';
                e.permStr = Nf_U2W(b);
            }
        }
        out.push_back(std::move(e));
    }
}

// Long listing format used by SFTP and FTP LIST fallback.
// "-rwxr-xr-x  1 user group  12345 Jan  1  2024 filename"
static void Nf_ParseLongListing(const std::string& data, std::vector<NeFtpEntry>& out)
{
    std::istringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // Skip "total NNN" lines
        if (line.size() >= 5 && line.substr(0, 5) == "total") continue;
        if (line.size() < 11) continue;

        NeFtpEntry e;
        char typeCh = line[0];
        e.isDir = (typeCh == 'd');
        e.permissions = Nf_ParsePermStr(line.substr(0, 10), e.permStr);

        // Tokenize to get fields 0..8+
        std::vector<std::string> tokens;
        {
            std::istringstream ls(line);
            std::string tok;
            while (ls >> tok) tokens.push_back(tok);
        }
        // Minimum: perms nlinks user group size month day year/time name
        if (tokens.size() < 9) continue;

        // Size is token[4]
        e.size = (int64_t)_atoi64(tokens[4].c_str());

        // Filename starts at token[8], may have spaces: re-find after 8 tokens
        {
            std::string tmp = line;
            int skipped = 0;
            size_t pos = 0;
            while (skipped < 8 && pos < tmp.size()) {
                // skip whitespace
                while (pos < tmp.size() && tmp[pos] == ' ') ++pos;
                // skip token
                while (pos < tmp.size() && tmp[pos] != ' ') ++pos;
                ++skipped;
            }
            while (pos < tmp.size() && tmp[pos] == ' ') ++pos;
            std::string nameStr = tmp.substr(pos);
            // Remove trailing \r
            if (!nameStr.empty() && nameStr.back() == '\r') nameStr.pop_back();
            // Handle symlinks: "link -> target"
            size_t arrow = nameStr.find(" -> ");
            if (arrow != std::string::npos) nameStr = nameStr.substr(0, arrow);
            if (nameStr == "." || nameStr == "..") continue;
            e.name = Nf_U2W(nameStr.c_str());
        }
        if (e.name.empty()) continue;
        out.push_back(std::move(e));
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void NeFtp_Init()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void NeFtp_DisconnectAll()
{
    for (auto& kv : s_conns) curl_easy_cleanup(kv.second.curl);
    s_conns.clear();
    s_activeId = 0;
    s_curl = nullptr;
    s_activeProfile = NeProfile{};
}

void NeFtp_Cleanup()
{
    NeFtp_DisconnectAll();
    curl_global_cleanup();
}

bool NeFtp_SetActiveConn(int64_t profileId)
{
    auto it = s_conns.find(profileId);
    if (it == s_conns.end()) return false;
    s_activeId      = profileId;
    s_curl          = it->second.curl;
    s_activeProfile = it->second.profile;
    return true;
}

bool NeFtp_Connect(const NeProfile& p)
{
    // Already connected to this profile — just make it active.
    if (s_conns.count(p.id)) {
        NeFtp_SetActiveConn(p.id);
        return true;
    }

    CURL* curl = curl_easy_init();
    if (!curl) { s_lastError = L"curl_easy_init failed"; return false; }

    // Temporarily point the shared pointers at the new handle so that
    // Nf_ApplyCommonOpts / NeFtp_ListDir work before we store it.
    s_curl = curl;
    s_activeProfile = p;
    Nf_ApplyCommonOpts();

    std::wstring initPath = p.initialPath.empty() ? L"/" : p.initialPath;
    std::vector<NeFtpEntry> test;
    if (!NeFtp_ListDir(initPath, test)) {
        curl_easy_cleanup(curl);
        s_curl = nullptr;
        s_activeProfile = NeProfile{};
        // Restore previous active connection (if any).
        if (s_activeId) NeFtp_SetActiveConn(s_activeId);
        return false;
    }

    s_conns[p.id] = { curl, p };
    s_activeId = p.id;
    return true;
}

void NeFtp_Disconnect()
{
    if (!s_activeId) return;
    auto it = s_conns.find(s_activeId);
    if (it != s_conns.end()) {
        curl_easy_cleanup(it->second.curl);
        s_conns.erase(it);
    }
    s_activeId = 0;
    s_curl = nullptr;
    s_activeProfile = NeProfile{};
    // If other connections exist, make one of them active.
    if (!s_conns.empty()) {
        auto first = s_conns.begin();
        s_activeId = first->first;
        s_curl = first->second.curl;
        s_activeProfile = first->second.profile;
    }
}

void NeFtp_Disconnect(int64_t profileId)
{
    if (!profileId) return;
    auto it = s_conns.find(profileId);
    if (it == s_conns.end()) return;
    curl_easy_cleanup(it->second.curl);
    s_conns.erase(it);
    if (s_activeId == profileId) {
        s_activeId = 0;
        s_curl = nullptr;
        s_activeProfile = NeProfile{};
        if (!s_conns.empty()) {
            auto first = s_conns.begin();
            s_activeId = first->first;
            s_curl = first->second.curl;
            s_activeProfile = first->second.profile;
        }
    }
}

bool NeFtp_IsConnected() { return s_curl != nullptr; }

bool NeFtp_IsConnected(int64_t profileId) { return s_conns.count(profileId) > 0; }

const NeProfile& NeFtp_GetActiveProfile() { return s_activeProfile; }

bool NeFtp_ListDir(const std::wstring& remotePath, std::vector<NeFtpEntry>& out)
{
    out.clear();
    if (!s_curl) return false;

    std::string data;
    Nf_ResetOpOpts(data);

    std::string url = Nf_BuildUrl(remotePath);
    if (url.back() != '/') url += '/';
    curl_easy_setopt(s_curl, CURLOPT_URL, url.c_str());

    bool isFtp = (s_activeProfile.protocol != L"SFTP");

    if (isFtp) {
        // Try MLSD first (gives UNIX.mode which we need for permissions).
        curl_easy_setopt(s_curl, CURLOPT_CUSTOMREQUEST, "MLSD");
        CURLcode rc = curl_easy_perform(s_curl);
        curl_easy_setopt(s_curl, CURLOPT_CUSTOMREQUEST, (char*)NULL);

        if (rc == CURLE_OK) {
            Nf_ParseMlsd(data, out);
            // Sort: dirs first, then files, both alphabetical.
            std::sort(out.begin(), out.end(), [](const NeFtpEntry& a, const NeFtpEntry& b){
                if (a.isDir != b.isDir) return a.isDir > b.isDir;
                return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
            });
            return true;
        }
        // Fall back to LIST.
        data.clear();
        curl_easy_setopt(s_curl, CURLOPT_WRITEDATA, &data);
    }

    CURLcode rc = curl_easy_perform(s_curl);
    if (rc != CURLE_OK) { Nf_SetLastError(rc); return false; }

    if (isFtp)
        Nf_ParseLongListing(data, out);
    else
        Nf_ParseLongListing(data, out);

    std::sort(out.begin(), out.end(), [](const NeFtpEntry& a, const NeFtpEntry& b){
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return true;
}

bool NeFtp_DownloadToTemp(const std::wstring& remotePath, std::wstring& outLocalPath)
{
    if (!s_curl) return false;

    // Build temp directory: %TEMP%\NSBEdit_remote\<friendlyName>
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"NSBEdit_remote";
    CreateDirectoryW(dir.c_str(), NULL);
    dir += L"\\" + s_activeProfile.friendlyName;
    CreateDirectoryW(dir.c_str(), NULL);

    // Extract filename
    std::wstring fname = remotePath;
    size_t sl = fname.rfind(L'/');
    if (sl != std::wstring::npos) fname = fname.substr(sl + 1);
    outLocalPath = dir + L"\\" + fname;

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, outLocalPath.c_str(), L"wb") != 0 || !fp) {
        s_lastError = L"Cannot create temp file";
        outLocalPath.clear();
        return false;
    }

    std::string dummy;
    Nf_ResetOpOpts(dummy);
    curl_easy_setopt(s_curl, CURLOPT_URL,           Nf_BuildUrl(remotePath).c_str());
    curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION, Nf_WriteFile);
    curl_easy_setopt(s_curl, CURLOPT_WRITEDATA,     fp);

    CURLcode rc = curl_easy_perform(s_curl);
    fclose(fp);

    if (rc != CURLE_OK) {
        Nf_SetLastError(rc);
        DeleteFileW(outLocalPath.c_str());
        outLocalPath.clear();
        return false;
    }
    return true;
}

bool NeFtp_Upload(const std::wstring& localPath, const std::wstring& remotePath)
{
    if (!s_curl) return false;

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, localPath.c_str(), L"rb") != 0 || !fp) {
        s_lastError = L"Cannot open local file for upload";
        return false;
    }

    _fseeki64(fp, 0, SEEK_END);
    curl_off_t fileSize = (curl_off_t)_ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);

    std::string dummy;
    Nf_ResetOpOpts(dummy);
    curl_easy_setopt(s_curl, CURLOPT_URL,             Nf_BuildUrl(remotePath).c_str());
    curl_easy_setopt(s_curl, CURLOPT_UPLOAD,          1L);
    curl_easy_setopt(s_curl, CURLOPT_READFUNCTION,    Nf_ReadFile);
    curl_easy_setopt(s_curl, CURLOPT_READDATA,        fp);
    curl_easy_setopt(s_curl, CURLOPT_INFILESIZE_LARGE, fileSize);
    // Discard server response
    curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION,   Nf_WriteBuf);
    curl_easy_setopt(s_curl, CURLOPT_WRITEDATA,       &dummy);

    CURLcode rc = curl_easy_perform(s_curl);
    fclose(fp);
    curl_easy_setopt(s_curl, CURLOPT_UPLOAD, 0L);

    if (rc != CURLE_OK) { Nf_SetLastError(rc); return false; }
    return true;
}

bool NeFtp_Chmod(const std::wstring& remotePath, int mode)
{
    if (!s_curl) return false;

    char octal[8] = {};
    sprintf_s(octal, "%03o", mode & 0777);

    std::string cmd;
    if (s_activeProfile.protocol == L"SFTP")
        cmd = std::string("chmod ") + octal + " " + Nf_W2U(remotePath);
    else
        cmd = std::string("SITE CHMOD ") + octal + " " + Nf_W2U(remotePath);

    struct curl_slist* cmds = curl_slist_append(NULL, cmd.c_str());

    // Get parent directory for the URL (we need a valid path for the request).
    std::wstring parent = remotePath;
    size_t sl = parent.rfind(L'/');
    if (sl != std::wstring::npos && sl > 0) parent = parent.substr(0, sl);
    else parent = L"/";

    std::string url = Nf_BuildUrl(parent);
    if (url.back() != '/') url += '/';

    std::string dummy;
    Nf_ResetOpOpts(dummy);
    curl_easy_setopt(s_curl, CURLOPT_URL,    url.c_str());
    curl_easy_setopt(s_curl, CURLOPT_QUOTE,  cmds);
    curl_easy_setopt(s_curl, CURLOPT_NOBODY, 1L);

    CURLcode rc = curl_easy_perform(s_curl);

    curl_slist_free_all(cmds);
    curl_easy_setopt(s_curl, CURLOPT_QUOTE,  (struct curl_slist*)NULL);
    curl_easy_setopt(s_curl, CURLOPT_NOBODY, 0L);

    if (rc != CURLE_OK) { Nf_SetLastError(rc); return false; }
    return true;
}

bool NeFtp_Delete(const std::wstring& remotePath, bool isDir)
{
    if (!s_curl) return false;

    std::string cmd;
    if (s_activeProfile.protocol == L"SFTP")
        cmd = isDir ? ("rmdir " + Nf_W2U(remotePath)) : ("rm " + Nf_W2U(remotePath));
    else
        cmd = isDir ? ("RMD "  + Nf_W2U(remotePath)) : ("DELE " + Nf_W2U(remotePath));

    struct curl_slist* cmds = curl_slist_append(NULL, cmd.c_str());

    std::wstring parent = remotePath;
    size_t sl = parent.rfind(L'/');
    if (sl != std::wstring::npos && sl > 0) parent = parent.substr(0, sl);
    else parent = L"/";

    std::string url = Nf_BuildUrl(parent);
    if (url.back() != '/') url += '/';

    std::string dummy;
    Nf_ResetOpOpts(dummy);
    curl_easy_setopt(s_curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(s_curl, CURLOPT_QUOTE,         cmds);
    curl_easy_setopt(s_curl, CURLOPT_NOBODY,        1L);
    curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION, Nf_WriteBuf);
    curl_easy_setopt(s_curl, CURLOPT_WRITEDATA,     &dummy);

    CURLcode rc = curl_easy_perform(s_curl);

    curl_slist_free_all(cmds);
    curl_easy_setopt(s_curl, CURLOPT_QUOTE,  (struct curl_slist*)NULL);
    curl_easy_setopt(s_curl, CURLOPT_NOBODY, 0L);

    if (rc != CURLE_OK) { Nf_SetLastError(rc); return false; }
    return true;
}

bool NeFtp_MkDir(const std::wstring& remotePath)
{
    if (!s_curl) return false;

    std::string cmd;
    if (s_activeProfile.protocol == L"SFTP")
        cmd = "mkdir " + Nf_W2U(remotePath);
    else
        cmd = "MKD "   + Nf_W2U(remotePath);

    struct curl_slist* cmds = curl_slist_append(NULL, cmd.c_str());

    std::wstring parent = remotePath;
    size_t sl = parent.rfind(L'/');
    if (sl != std::wstring::npos && sl > 0) parent = parent.substr(0, sl);
    else parent = L"/";

    std::string url = Nf_BuildUrl(parent);
    if (url.back() != '/') url += '/';

    std::string dummy;
    Nf_ResetOpOpts(dummy);
    curl_easy_setopt(s_curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(s_curl, CURLOPT_QUOTE,         cmds);
    curl_easy_setopt(s_curl, CURLOPT_NOBODY,        1L);
    curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION, Nf_WriteBuf);
    curl_easy_setopt(s_curl, CURLOPT_WRITEDATA,     &dummy);

    CURLcode rc = curl_easy_perform(s_curl);

    curl_slist_free_all(cmds);
    curl_easy_setopt(s_curl, CURLOPT_QUOTE,  (struct curl_slist*)NULL);
    curl_easy_setopt(s_curl, CURLOPT_NOBODY, 0L);

    if (rc != CURLE_OK) { Nf_SetLastError(rc); return false; }
    return true;
}

bool NeFtp_CreateEmptyFile(const std::wstring& remotePath)
{
    if (!s_curl) return false;

    // Upload a zero-byte temp file
    wchar_t tmpDir[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    GetTempFileNameW(tmpDir, L"nef", 0, tmpFile);

    // Truncate to zero bytes
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, tmpFile, L"wb") != 0 || !fp) {
        s_lastError = L"Cannot create temp file";
        return false;
    }
    fclose(fp);

    FILE* fpIn = nullptr;
    _wfopen_s(&fpIn, tmpFile, L"rb");

    std::string dummy;
    Nf_ResetOpOpts(dummy);
    curl_easy_setopt(s_curl, CURLOPT_URL,              Nf_BuildUrl(remotePath).c_str());
    curl_easy_setopt(s_curl, CURLOPT_UPLOAD,           1L);
    curl_easy_setopt(s_curl, CURLOPT_READFUNCTION,     Nf_ReadFile);
    curl_easy_setopt(s_curl, CURLOPT_READDATA,         fpIn);
    curl_easy_setopt(s_curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)0);
    curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION,    Nf_WriteBuf);
    curl_easy_setopt(s_curl, CURLOPT_WRITEDATA,        &dummy);

    CURLcode rc = curl_easy_perform(s_curl);
    if (fpIn) fclose(fpIn);
    curl_easy_setopt(s_curl, CURLOPT_UPLOAD, 0L);
    DeleteFileW(tmpFile);

    if (rc != CURLE_OK) { Nf_SetLastError(rc); return false; }
    return true;
}

std::wstring NeFtp_GetLastError() { return s_lastError; }
