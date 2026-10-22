@echo off
setlocal
cd /d "%~dp0.."
set "LLVM_BIN=%~1"
if not defined LLVM_BIN set "LLVM_BIN=%LLVM_MINGW_BIN%"
if not exist "%LLVM_BIN%\x86_64-w64-mingw32-clang++.exe" (
 echo Supply the official llvm-mingw bin directory as the first argument.
 exit /b 1
)
if not exist "native\out" mkdir "native\out"
if not exist "dist" mkdir "dist"
if /i "%~2"=="full" goto full
"%LLVM_BIN%\x86_64-w64-mingw32-windres.exe" -DWIN11USB_READONLY -I native/resources -I assets native/resources/app.rc native/out/preview-res.o
if errorlevel 1 exit /b 1
"%LLVM_BIN%\x86_64-w64-mingw32-clang++.exe" -std=c++17 -O2 -static -municode -mwindows -DWIN11USB_READONLY -D_WIN32_WINNT=0x0A00 -Inative/include native/src/main.cpp native/src/common.cpp native/src/disk.cpp native/src/ui.cpp native/out/preview-res.o -o dist/Win11Usb-NativePreview.exe -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lshell32 -lole32 -ladvapi32 -lsetupapi -lcfgmgr32 -luuid -Wl,--dynamicbase,--nxcompat,--high-entropy-va
if errorlevel 1 exit /b 1
echo Native read-only preview built: dist\Win11Usb-NativePreview.exe
copy /y "dist\Win11Usb-NativePreview.exe" "dist\Win11Usb-NativeReadOnly-v0.2.0-preview.5.exe" >nul
if errorlevel 1 exit /b 1
exit /b 0
:full
"%LLVM_BIN%\x86_64-w64-mingw32-clang.exe" -O2 -static native/third_party/fat32format.c -o native/out/fat32format-x64.exe
if errorlevel 1 exit /b 1
"%LLVM_BIN%\x86_64-w64-mingw32-windres.exe" -I native/resources -I assets native/resources/app.rc native/out/full-res.o
if errorlevel 1 exit /b 1
"%LLVM_BIN%\x86_64-w64-mingw32-clang++.exe" -std=c++17 -O2 -static -municode -mwindows -D_WIN32_WINNT=0x0A00 -Inative/include native/src/main.cpp native/src/common.cpp native/src/disk.cpp native/src/ui.cpp native/src/policy.cpp native/src/workflow.cpp native/src/files.cpp native/src/process.cpp native/src/wim.cpp native/src/workspace.cpp native/src/backend.cpp native/out/full-res.o -o dist/Win11Usb-NativeReview-v0.2.0-preview.5.exe -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lshell32 -lole32 -ladvapi32 -lsetupapi -lcfgmgr32 -luuid -Wl,--dynamicbase,--nxcompat,--high-entropy-va
if errorlevel 1 exit /b 1
echo Native workflow review built: dist\Win11Usb-NativeReview-v0.2.0-preview.5.exe
exit /b 0
