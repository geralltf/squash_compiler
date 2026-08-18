@echo off
cd /d C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
echo VCVARS_DONE > msvc_build_log.txt
cl /nologo /O2 /W0 squash.c /Fe:squash_msvc.exe >> msvc_build_log.txt 2>&1
echo CL_EXIT=%ERRORLEVEL% >> msvc_build_log.txt
if exist squash_msvc.exe (echo FILE_OK >> msvc_build_log.txt) else (echo FILE_MISSING >> msvc_build_log.txt)
