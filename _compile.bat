@rem compiles with GCC 12.2.0 + MinGW-w64 10.0.0 (UCRT) - https://winlibs.com/

@cd /d "%~dp0"

@if exist %~dp0..\mingw64\bin\ set path=%path%;%~dp0..\mingw64\bin

@if exist %~dp0ftpdmin.exe del ftpdmin.exe

gcc.exe ftpdmin.c paths.c -lws2_32 -s -o ftpdmin.exe

@pause
