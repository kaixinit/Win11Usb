@echo off
setlocal
cd /d "%~dp0.."
set "LLVM_BIN=%~1"
if not defined LLVM_BIN set "LLVM_BIN=%LLVM_MINGW_BIN%"
if not exist "native\out" mkdir "native\out"
"%LLVM_BIN%\x86_64-w64-mingw32-clang++.exe" -std=c++17 -O1 -g -static -municode -D_WIN32_WINNT=0x0A00 -Inative/include native/tests/tests.cpp native/src/common.cpp native/src/disk.cpp native/src/policy.cpp native/src/workflow.cpp native/src/files.cpp native/src/process.cpp native/src/wim.cpp native/src/workspace.cpp native/src/disk_rebuild.cpp -o native/out/Win11Usb-NativeTests.exe -lsetupapi -lcfgmgr32 -ladvapi32 -lole32 -lshell32 -luuid
if errorlevel 1 exit /b 1
set "RESULTS=work\native-tests-%RANDOM%-%RANDOM%"
native\out\Win11Usb-NativeTests.exe native\tests\identity-fixtures.tsv "%RESULTS%"
exit /b %ERRORLEVEL%
