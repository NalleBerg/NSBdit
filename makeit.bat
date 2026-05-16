@echo on
setlocal

:: Kill running instance so exe can be replaced
taskkill /F /IM NSBEdit.exe >nul 2>&1

:: Compile resource
windres NSBEdit.rc -o NSBEdit.res --output-format=coff
if %ERRORLEVEL% neq 0 ( echo [ERROR] windres failed & pause & exit /b 1 )

:: Compile sqlite3 as C
gcc -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_DEFAULT_MEMSTATUS=0 -c sqlite3\sqlite3.c -o sqlite3\sqlite3.o
if %ERRORLEVEL% neq 0 ( echo [ERROR] sqlite3 compile failed & pause & exit /b 1 )

:: Compile QUIC stubs (satisfies ngtcp2/nghttp3 refs in libcurl; never called at runtime)
gcc -O2 -c curl\lib\quic_stubs.c -o curl\lib\quic_stubs.o
if %ERRORLEVEL% neq 0 ( echo [ERROR] quic_stubs compile failed & pause & exit /b 1 )

:: Compile and link
g++ -std=c++17 -O2 -mwindows -municode ^
    -I. -Isqlite3 -Icurl\include ^
    -DCURL_STATICLIB ^
    -Iscintilla_src\scintilla\include -Ilexilla_src\lexilla\include ^
    NSBEdit.cpp ne_tabs.cpp ne_statusbar.cpp dpi.cpp tooltip\tooltip.cpp scroll\my_scrollbar_vscroll.cpp ^
    highlight\highlight.cpp checkbox.cpp ^
    ne_crypto.cpp ne_profiles.cpp ne_ftp.cpp ^
    sqlite3\sqlite3.o curl\lib\quic_stubs.o NSBEdit.res ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -luser32 -lgdi32 -lgdiplus -lwinspool ^
    -Lscintilla_src\scintilla\bin -Llexilla_src\lexilla\bin -lscintilla -llexilla ^
    -limm32 -loleaut32 -ladvapi32 -lole32 -luuid ^
    -Lcurl\lib -lcurl -lssh2 -lssl -lcrypto -lz -lnghttp2 -lbrotlidec -lbrotlicommon -lpsl -lzstd ^
    -lws2_32 -lcrypt32 -lbcrypt -lwinhttp -lwldap32 -lsecur32 -liphlpapi -lntdll ^
    -static -static-libgcc -static-libstdc++ ^
    -o NSBEdit.exe

if %ERRORLEVEL% neq 0 ( echo. & echo BUILD FAILED & pause & exit /b 1 )

::Keep runtime assets alongside the exe (already in repo; no-op copy)
copy /Y NSB.png NSB.png >nul
copy /Y curver.txt curver.txt >nul 2>&1

echo.
echo Done - NSBEdit.exe updated
