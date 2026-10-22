@echo off
setlocal
cd /d "%~dp0.."
set "LLVM_BIN=%~1"
if not defined LLVM_BIN set "LLVM_BIN=%LLVM_MINGW_BIN%"
if not exist "native\out" mkdir "native\out"
"%LLVM_BIN%\x86_64-w64-mingw32-clang++.exe" -std=c++17 -O1 -g -static -municode -D_WIN32_WINNT=0x0A00 -Inative/include native/tests/tests.cpp native/tests/eject_tests.cpp native/src/common.cpp native/src/disk.cpp native/src/policy.cpp native/src/workflow.cpp native/src/files.cpp native/src/process.cpp native/src/wim.cpp native/src/workspace.cpp native/src/disk_rebuild.cpp native/src/postformat.cpp native/src/eject.cpp -o native/out/Win11Usb-NativeTests.exe -lsetupapi -lcfgmgr32 -ladvapi32 -lole32 -lshell32 -luuid
if errorlevel 1 exit /b 1
set "RESULTS=work\native-tests-%RANDOM%-%RANDOM%"
native\out\Win11Usb-NativeTests.exe native\tests\identity-fixtures.tsv "%RESULTS%"
if errorlevel 1 exit /b 1
"%LLVM_BIN%\x86_64-w64-mingw32-clang.exe" -O1 -g -static native/tests/fat32format_tests.c -o native/out/Win11Usb-Fat32Tests.exe
if errorlevel 1 exit /b 1
set "FAT_RESULTS=work\native-formatter-tests-%RANDOM%-%RANDOM%"
native\out\Win11Usb-Fat32Tests.exe "%FAT_RESULTS%"
if errorlevel 1 exit /b 1
native\out\Win11Usb-Fat32Tests.exe --invalid-seek >"%FAT_RESULTS%\invalid-seek.txt" 2>&1
if not errorlevel 1 exit /b 1
echo PASS formatter invalid-handle seek returns failure before any write
native\out\Win11Usb-Fat32Tests.exe --lock-timeout "%FAT_RESULTS%" >"%FAT_RESULTS%\lock-timeout.txt" 2>&1
if not "%ERRORLEVEL%"=="32" exit /b 1
native\out\Win11Usb-Fat32Tests.exe --verify-unwritten "%FAT_RESULTS%"
if errorlevel 1 exit /b 1
exit /b 0
