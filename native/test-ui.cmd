@echo off
setlocal
cd /d "%~dp0.."
set "LLVM_BIN=%~1"
if not defined LLVM_BIN set "LLVM_BIN=%LLVM_MINGW_BIN%"
if not exist "native\out" mkdir "native\out"
"%LLVM_BIN%\x86_64-w64-mingw32-windres.exe" -DWIN11USB_READONLY -I native/resources -I assets native/resources/app.rc native/out/ui-test-res.o
if errorlevel 1 exit /b 1
"%LLVM_BIN%\x86_64-w64-mingw32-clang++.exe" -std=c++17 -O1 -g -static -municode -DWIN11USB_READONLY -D_WIN32_WINNT=0x0A00 -Inative/include native/tests/ui_refresh.cpp native/src/common.cpp native/src/disk.cpp native/src/dialog.cpp native/src/dialog_specs.cpp native/src/eject.cpp native/src/eject_report.cpp native/out/ui-test-res.o -o native/out/Win11Usb-UiTests.exe -lcomctl32 -lcomdlg32 -lgdi32 -lgdiplus -luser32 -lshell32 -lole32 -ladvapi32 -lsetupapi -lcfgmgr32 -luuid
if errorlevel 1 exit /b 1
native\out\Win11Usb-UiTests.exe "work\native-ui-tests-%RANDOM%-%RANDOM%"
exit /b %ERRORLEVEL%
