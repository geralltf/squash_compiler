@echo off
cd /d C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
echo [1] Building squash_msvc.exe with MSVC...
cl /nologo /O2 /W0 squash.c /Fe:squash_msvc.exe
echo MSVC_EXIT=%ERRORLEVEL%
if not exist squash_msvc.exe (echo MSVC BUILD FAILED && exit /b 1)
echo [2] Building squash2_new.exe with squash_msvc.exe...
squash_msvc.exe squash.c -o squash2_new.exe
echo S2_EXIT=%ERRORLEVEL%
if not exist squash2_new.exe (echo S2 BUILD FAILED && exit /b 1)
echo [3] Building test_program2_new.exe with squash2_new.exe...
squash2_new.exe test_program2.c -o test_program2_new.exe
echo TP2_EXIT=%ERRORLEVEL%
echo [4] Running test_program2_new.exe...
test_program2_new.exe
echo RUN_EXIT=%ERRORLEVEL%
