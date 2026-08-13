@echo off
:: Platform: Windows (native Win32 console)
:: Compiler: gcc (MinGW-w64)
:: Install:  https://winlibs.com  or  winget install mingw
:: Output:   forever-text.exe

cd /d "%~dp0.."

gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os ^
    -Isrc ^
    -o forever-text.exe ^
    src/main.c src/editor.c src/buffer.c src/platform/win32.c

echo Built: forever-text.exe
