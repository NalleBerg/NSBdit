#include "ne_profiles.h"
#include "ne_crypto.h"
#include "sqlite3/sqlite3.h"
#include <shlobj.h>
#include <time.h>
#include <string>
#include <vector>

static sqlite3* s_db = nullptr;

// ── String helpers ────────────────────────────────────────────────────────────
static std::string Np_W2U(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, NULL, NULL);
    return s;
}

static std::wstring Np_U2W(const char* u)
{
    if (!u || !*u) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, NULL, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u, -1, w.data(), n);
    return w;
}

// ── DB path ───────────────────────────────────────────────────────────────────
static std::wstring Np_GetDbPath()
{
    // Portable: if local nsbedit.db already exists, use it.
    if (GetFileAttributesW(L"nsbedit.db") != INVALID_FILE_ATTRIBUTES)
        return L"nsbedit.db";

    // Otherwise use %APPDATA%\NSBEdit\nsbedit.db.
    wchar_t appdata[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\NSBEdit";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\nsbedit.db";
    }
    return L"nsbedit.db";
}

// ── Init / close ──────────────────────────────────────────────────────────────
bool NeProfiles_Init()
{
    if (s_db) return true;
    std::string path = Np_W2U(Np_GetDbPath());
    if (sqlite3_open(path.c_str(), &s_db) != SQLITE_OK) {
        s_db = nullptr;
        return false;
    }
    const char* schema =
        "CREATE TABLE IF NOT EXISTS profiles ("
        "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  friendly_name TEXT    NOT NULL,"
        "  protocol      TEXT    NOT NULL DEFAULT 'FTP',"
        "  host          TEXT    NOT NULL,"
        "  port          INTEGER NOT NULL DEFAULT 21,"
        "  username      TEXT    NOT NULL DEFAULT '',"
        "  password      BLOB,"
        "  remember_pwd  INTEGER NOT NULL DEFAULT 0,"
        "  initial_path  TEXT    NOT NULL DEFAULT '/',"
        "  created       INTEGER NOT NULL DEFAULT 0,"
        "  modified      INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ");";
    char* err = nullptr;
    if (sqlite3_exec(s_db, schema, NULL, NULL, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        sqlite3_close(s_db);
        s_db = nullptr;
        return false;
    }
    return true;
}

void NeProfiles_Close()
{
    if (s_db) { sqlite3_close(s_db); s_db = nullptr; }
}

// ── Generic settings ─────────────────────────────────────────────────────
bool NeProfiles_GetIntSetting(const char* key, int defaultValue, int& out)
{
    out = defaultValue;
    if (!s_db || !key) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(s_db, "SELECT value FROM settings WHERE key=?", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* v = (const char*)sqlite3_column_text(stmt, 0);
        if (v) { out = atoi(v); found = true; }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool NeProfiles_SetIntSetting(const char* key, int value)
{
    if (!s_db || !key) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(s_db,
        "INSERT INTO settings(key,value) VALUES(?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    char buf[32]; _itoa_s(value, buf, 10);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, buf, -1, SQLITE_STATIC);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

// ── Password helpers ──────────────────────────────────────────────────────────
static void Np_EncryptPw(const std::wstring& pw, std::vector<BYTE>& out)
{
    out.clear();
    if (pw.empty()) return;
    std::string utf8 = Np_W2U(pw);
    NeCrypto_Encrypt(utf8.data(), utf8.size(), out);
}

static std::wstring Np_DecryptPw(const void* blob, int len)
{
    if (!blob || len <= 0) return {};
    std::vector<BYTE> plain;
    if (!NeCrypto_Decrypt(blob, (size_t)len, plain)) return {};
    plain.push_back(0);
    return Np_U2W((const char*)plain.data());
}

// ── Row → NeProfile ───────────────────────────────────────────────────────────
// Column order: id, friendly_name, protocol, host, port, username,
//               password, remember_pwd, initial_path
static NeProfile Np_RowToProfile(sqlite3_stmt* st)
{
    NeProfile p;
    p.id             = sqlite3_column_int64(st, 0);
    p.friendlyName   = Np_U2W((const char*)sqlite3_column_text(st, 1));
    p.protocol       = Np_U2W((const char*)sqlite3_column_text(st, 2));
    p.host           = Np_U2W((const char*)sqlite3_column_text(st, 3));
    p.port           = sqlite3_column_int(st, 4);
    p.username       = Np_U2W((const char*)sqlite3_column_text(st, 5));
    p.rememberPassword = (sqlite3_column_int(st, 7) != 0);
    p.initialPath    = Np_U2W((const char*)sqlite3_column_text(st, 8));
    if (p.rememberPassword)
        p.password = Np_DecryptPw(sqlite3_column_blob(st, 6),
                                   sqlite3_column_bytes(st, 6));
    return p;
}

// ── CRUD ──────────────────────────────────────────────────────────────────────
bool NeProfiles_Add(NeProfile& p)
{
    if (!s_db) return false;
    std::vector<BYTE> encPw;
    if (p.rememberPassword && !p.password.empty())
        Np_EncryptPw(p.password, encPw);

    const char* sql =
        "INSERT INTO profiles"
        " (friendly_name,protocol,host,port,username,password,remember_pwd,initial_path,created,modified)"
        " VALUES (?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return false;

    int64_t now = (int64_t)time(NULL);
    sqlite3_bind_text(st, 1, Np_W2U(p.friendlyName).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, Np_W2U(p.protocol).c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, Np_W2U(p.host).c_str(),         -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 4, p.port);
    sqlite3_bind_text(st, 5, Np_W2U(p.username).c_str(),     -1, SQLITE_TRANSIENT);
    if (!encPw.empty())
        sqlite3_bind_blob(st, 6, encPw.data(), (int)encPw.size(), SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(st, 6);
    sqlite3_bind_int  (st, 7, p.rememberPassword ? 1 : 0);
    sqlite3_bind_text (st, 8, Np_W2U(p.initialPath).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 9, now);
    sqlite3_bind_int64(st,10, now);

    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    if (ok) p.id = sqlite3_last_insert_rowid(s_db);
    sqlite3_finalize(st);
    return ok;
}

bool NeProfiles_Update(const NeProfile& p)
{
    if (!s_db) return false;
    std::vector<BYTE> encPw;
    if (p.rememberPassword && !p.password.empty())
        Np_EncryptPw(p.password, encPw);

    const char* sql =
        "UPDATE profiles SET friendly_name=?,protocol=?,host=?,port=?,username=?,"
        "password=?,remember_pwd=?,initial_path=?,modified=? WHERE id=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return false;

    int64_t now = (int64_t)time(NULL);
    sqlite3_bind_text (st, 1, Np_W2U(p.friendlyName).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 2, Np_W2U(p.protocol).c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 3, Np_W2U(p.host).c_str(),         -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (st, 4, p.port);
    sqlite3_bind_text (st, 5, Np_W2U(p.username).c_str(),     -1, SQLITE_TRANSIENT);
    if (!encPw.empty())
        sqlite3_bind_blob(st, 6, encPw.data(), (int)encPw.size(), SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(st, 6);
    sqlite3_bind_int  (st, 7, p.rememberPassword ? 1 : 0);
    sqlite3_bind_text (st, 8, Np_W2U(p.initialPath).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 9, now);
    sqlite3_bind_int64(st,10, p.id);

    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

bool NeProfiles_Delete(int64_t id)
{
    if (!s_db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(s_db, "DELETE FROM profiles WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, id);
    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

bool NeProfiles_List(std::vector<NeProfile>& out)
{
    out.clear();
    if (!s_db) return false;
    const char* sql =
        "SELECT id,friendly_name,protocol,host,port,username,password,remember_pwd,initial_path"
        " FROM profiles ORDER BY friendly_name COLLATE NOCASE;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    while (sqlite3_step(st) == SQLITE_ROW)
        out.push_back(Np_RowToProfile(st));
    sqlite3_finalize(st);
    return true;
}

bool NeProfiles_GetById(int64_t id, NeProfile& out)
{
    if (!s_db) return false;
    const char* sql =
        "SELECT id,friendly_name,protocol,host,port,username,password,remember_pwd,initial_path"
        " FROM profiles WHERE id=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(s_db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, id);
    bool ok = false;
    if (sqlite3_step(st) == SQLITE_ROW) { out = Np_RowToProfile(st); ok = true; }
    sqlite3_finalize(st);
    return ok;
}
