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
"%LLVM_BIN%\x86_64-w64-mingw32-windres.exe" -DWIN11USB_READONLY -I native/resources -I assets native/resources/app.rc native/out/visual-qa-res.o
if errorlevel 1 exit /b 1
"%LLVM_BIN%\x86_64-w64-mingw32-clang++.exe" -std=c++17 -O2 -static -municode -mwindows -DWIN11USB_READONLY -D_WIN32_WINNT=0x0A00 -Inative/include native/tests/visual_qa.cpp native/src/common.cpp native/src/disk.cpp native/src/dialog.cpp native/src/dialog_specs.cpp native/out/visual-qa-res.o -o native/out/Win11Usb-VisualQA.exe -lcomctl32 -lcomdlg32 -lgdi32 -lgdiplus -luser32 -lshell32 -lole32 -ladvapi32 -lsetupapi -lcfgmgr32 -luuid -Wl,--dynamicbase,--nxcompat,--high-entropy-va
if errorlevel 1 exit /b 1
echo Built read-only visual fixture: native\out\Win11Usb-VisualQA.exe
echo Scenes: ready running start start-ntfs format ntfs cleanup usb iso success warning stop
echo Launch with a scene argument, or use Alt+Space followed by 1-9, A, B, C.
exit /b 0
