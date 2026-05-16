#pragma once
#include <windows.h>
#include <vector>

// BCrypt AES-256-CBC encryption with DPAPI-wrapped master key stored in
// %APPDATA%\NSBEdit\mk.bin  (falls back to .\mk.bin for portable use).
// Call NeCrypto_Init() before any Encrypt/Decrypt calls.
// Call NeCrypto_Close() at shutdown to zero the key from memory.

bool NeCrypto_Init();
void NeCrypto_Close();

// Output format: [16-byte IV][ciphertext with PKCS7 padding].
bool NeCrypto_Encrypt(const void* plain, size_t len, std::vector<BYTE>& out);
bool NeCrypto_Decrypt(const void* cipher, size_t len, std::vector<BYTE>& out);
