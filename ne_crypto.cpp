#include "ne_crypto.h"
#include <bcrypt.h>
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#include <wincrypt.h>  // CryptProtectData / CryptUnprotectData
#include <shlobj.h>
#include <string>

// ── Internal state ────────────────────────────────────────────────────────────
static BYTE  s_masterKey[32] = {};
static bool  s_cryptoInit    = false;

// Entropy blob used with DPAPI so the wrapped key is bound to this application.
static const BYTE k_entropy[]  = { 'N','S','B','E','d','i','t','-','m','k','-','v','1' };
static const DWORD k_entropyLen = sizeof(k_entropy);

// ── Master-key path ───────────────────────────────────────────────────────────
static std::wstring Ne_GetMkPath()
{
    wchar_t appdata[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\NSBEdit";
        CreateDirectoryW(dir.c_str(), NULL);            // no-op if exists
        return dir + L"\\mk.bin";
    }
    return L"mk.bin";                                   // portable fallback
}

// ── Load or generate the 32-byte master key ───────────────────────────────────
static bool Ne_LoadOrCreateMasterKey()
{
    std::wstring path = Ne_GetMkPath();

    // ── Try to load existing wrapped key ──
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hf, NULL);
        if (sz > 0 && sz < 65536) {
            std::vector<BYTE> blob(sz);
            DWORD rd = 0;
            ReadFile(hf, blob.data(), sz, &rd, NULL);
            CloseHandle(hf);

            DATA_BLOB in  = { rd, blob.data() };
            DATA_BLOB ent = { k_entropyLen, (BYTE*)k_entropy };
            DATA_BLOB out = {};
            if (CryptUnprotectData(&in, NULL, &ent, NULL, NULL, 0, &out)) {
                if (out.cbData == 32) {
                    memcpy(s_masterKey, out.pbData, 32);
                    LocalFree(out.pbData);
                    return true;
                }
                LocalFree(out.pbData);
            }
        } else {
            CloseHandle(hf);
        }
    }

    // ── Generate a fresh 32-byte master key via BCrypt RNG ──
    BCRYPT_ALG_HANDLE hRng = NULL;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hRng, BCRYPT_RNG_ALGORITHM, NULL, 0)))
        return false;
    NTSTATUS ns = BCryptGenRandom(hRng, s_masterKey, 32, 0);
    BCryptCloseAlgorithmProvider(hRng, 0);
    if (!NT_SUCCESS(ns)) return false;

    // ── Wrap with DPAPI and persist ──
    DATA_BLOB plain   = { 32, s_masterKey };
    DATA_BLOB ent     = { k_entropyLen, (BYTE*)k_entropy };
    DATA_BLOB wrapped = {};
    if (!CryptProtectData(&plain, L"NSBEdit", &ent, NULL, NULL, 0, &wrapped))
        return false;

    hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(hf, wrapped.pbData, wrapped.cbData, &wr, NULL);
        CloseHandle(hf);
    }
    LocalFree(wrapped.pbData);
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────
bool NeCrypto_Init()
{
    if (s_cryptoInit) return true;
    s_cryptoInit = Ne_LoadOrCreateMasterKey();
    return s_cryptoInit;
}

void NeCrypto_Close()
{
    SecureZeroMemory(s_masterKey, sizeof(s_masterKey));
    s_cryptoInit = false;
}

bool NeCrypto_Encrypt(const void* plain, size_t len, std::vector<BYTE>& out)
{
    if (!s_cryptoInit) return false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    bool ok = false;

    do {
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0)))
            break;
        const WCHAR* mode = BCRYPT_CHAIN_MODE_CBC;
        if (!NT_SUCCESS(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                (PUCHAR)mode, (DWORD)((wcslen(mode) + 1) * sizeof(WCHAR)), 0)))
            break;
        if (!NT_SUCCESS(BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0,
                s_masterKey, 32, 0)))
            break;

        // Random IV
        BYTE iv[16] = {};
        BCRYPT_ALG_HANDLE hRng = NULL;
        BCryptOpenAlgorithmProvider(&hRng, BCRYPT_RNG_ALGORITHM, NULL, 0);
        BCryptGenRandom(hRng, iv, 16, 0);
        BCryptCloseAlgorithmProvider(hRng, 0);

        // Determine ciphertext size
        ULONG cbOut = 0;
        // BCryptEncrypt with NULL output does NOT modify iv, so we can reuse it.
        if (!NT_SUCCESS(BCryptEncrypt(hKey, (PUCHAR)plain, (ULONG)len,
                NULL, iv, 16, NULL, 0, &cbOut, BCRYPT_BLOCK_PADDING)))
            break;

        out.resize(16 + cbOut);
        memcpy(out.data(), iv, 16);

        ULONG written = 0;
        if (!NT_SUCCESS(BCryptEncrypt(hKey, (PUCHAR)plain, (ULONG)len,
                NULL, iv, 16, out.data() + 16, cbOut, &written, BCRYPT_BLOCK_PADDING)))
            break;

        out.resize(16 + written);
        ok = true;
    } while (false);

    if (hKey) BCryptDestroyKey(hKey);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

bool NeCrypto_Decrypt(const void* cipher, size_t len, std::vector<BYTE>& out)
{
    if (!s_cryptoInit || len < 17) return false;

    const BYTE* p = (const BYTE*)cipher;
    BYTE iv[16];
    memcpy(iv, p, 16);

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    bool ok = false;

    do {
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0)))
            break;
        const WCHAR* mode = BCRYPT_CHAIN_MODE_CBC;
        if (!NT_SUCCESS(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                (PUCHAR)mode, (DWORD)((wcslen(mode) + 1) * sizeof(WCHAR)), 0)))
            break;
        if (!NT_SUCCESS(BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0,
                s_masterKey, 32, 0)))
            break;

        ULONG cbOut = 0;
        if (!NT_SUCCESS(BCryptDecrypt(hKey, (PUCHAR)(p + 16), (ULONG)(len - 16),
                NULL, iv, 16, NULL, 0, &cbOut, BCRYPT_BLOCK_PADDING)))
            break;

        out.resize(cbOut);
        ULONG written = 0;
        if (!NT_SUCCESS(BCryptDecrypt(hKey, (PUCHAR)(p + 16), (ULONG)(len - 16),
                NULL, iv, 16, out.data(), cbOut, &written, BCRYPT_BLOCK_PADDING)))
            break;

        out.resize(written);
        ok = true;
    } while (false);

    if (hKey) BCryptDestroyKey(hKey);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}
