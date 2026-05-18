@echo off
setlocal enabledelayedexpansion
echo ============================================================
echo  NSBEdit build started
echo ============================================================

:: ── Kill running instance ────────────────────────────────────
echo [1/5] Killing running NSBEdit.exe (if any)...
taskkill /F /IM NSBEdit.exe >nul 2>&1
if !ERRORLEVEL! equ 0 (
    echo       Killed. Waiting for file handles to release...
    timeout /T 1 /NOBREAK >nul
) else (
    echo       Not running, nothing to kill.
)
echo.

:: ── Compile resource ─────────────────────────────────────────
echo [2/5] Compiling resource (windres)...
windres NSBEdit.rc -o NSBEdit.res --output-format=coff
if !ERRORLEVEL! neq 0 (
    echo [ERROR] windres FAILED  ^(exit code !ERRORLEVEL!^)
    exit /b 1
)
echo       Done.
echo.

:: ── Compile sqlite3 ──────────────────────────────────────────
echo [3/5] Compiling sqlite3...
gcc -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_DEFAULT_MEMSTATUS=0 -c sqlite3\sqlite3.c -o sqlite3\sqlite3.o
if !ERRORLEVEL! neq 0 (
    echo [ERROR] sqlite3 compile FAILED  ^(exit code !ERRORLEVEL!^)
    exit /b 1
)
echo       Done.
echo.

:: ── Compile QUIC stubs ───────────────────────────────────────
echo [4/5] Compiling QUIC stubs...
gcc -O2 -c curl\lib\quic_stubs.c -o curl\lib\quic_stubs.o
if !ERRORLEVEL! neq 0 (
    echo [ERROR] quic_stubs compile FAILED  ^(exit code !ERRORLEVEL!^)
    exit /b 1
)
echo       Done.
echo.

:: ── Compile + link NSBEdit ───────────────────────────────────
echo [5/5] Compiling and linking NSBEdit.cpp...
echo       (this is the slow step - warnings and errors appear below)
echo ............................................................
g++ -std=c++17 -O2 -mwindows -municode ^
    -I. -Isqlite3 -Icurl\include ^
    -DCURL_STATICLIB ^
    -Iscintilla_src\scintilla\include -Ilexilla_src\lexilla\include ^
    NSBEdit.cpp ne_tabs.cpp ne_statusbar.cpp dpi.cpp tooltip\tooltip.cpp scroll\my_scrollbar_vscroll.cpp ^
    highlight\highlight.cpp checkbox.cpp ^
    ne_crypto.cpp ne_profiles.cpp ne_ftp.cpp ne_autocomplete\ne_autocomplete.cpp ^
    rtf2html\ne_rtf2html_lib.cpp ^
    sqlite3\sqlite3.o curl\lib\quic_stubs.o NSBEdit.res ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -luser32 -lgdi32 -lgdiplus -lwinspool -lmsimg32 ^
    -Lscintilla_src\scintilla\bin -Llexilla_src\lexilla\bin -lscintilla -llexilla ^
    -limm32 -loleaut32 -ladvapi32 -lole32 -luuid ^
    -Lcurl\lib -lcurl -lssh2 -lssl -lcrypto -lz -lnghttp2 -lbrotlidec -lbrotlicommon -lpsl -lzstd ^
    -lws2_32 -lcrypt32 -lbcrypt -lwinhttp -lwldap32 -lsecur32 -liphlpapi -lntdll ^
    -static -static-libgcc -static-libstdc++ ^
    -o NSBEdit.exe
echo ............................................................
if !ERRORLEVEL! neq 0 (
    echo.
    echo [ERROR] Compile/link FAILED  ^(exit code !ERRORLEVEL!^)
    exit /b 1
)
echo       Done.
echo.

:: ── Assets ───────────────────────────────────────────────────
copy /Y NSB.png NSB.png >nul 2>&1
copy /Y curver.txt curver.txt >nul 2>&1

echo ============================================================
echo  BUILD SUCCEEDED  --  NSBEdit.exe updated
echo ============================================================
