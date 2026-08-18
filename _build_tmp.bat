@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler"
cl /nologo /O2 /W0 squash.c /Fe:squash_msvc.exe
echo CL_EXIT=%ERRORLEVEL%
