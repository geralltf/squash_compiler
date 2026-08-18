@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
echo Building squash_msvc.exe...
cl /nologo /O2 /W0 squash.c /Fe:squash_msvc.exe 2>&1
echo MSVC_EXIT=%ERRORLEVEL%
if not exist squash_msvc.exe (echo MSVC BUILD FAILED && exit /b 1)
echo Building squash2_new.exe...
squash_msvc.exe squash.c -o squash2_new.exe
echo S2_EXIT=%ERRORLEVEL%
echo Testing sizeof short...
squash2_new.exe C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler\test_short_s2.c -o test_short_s2.exe
echo TP_EXIT=%ERRORLEVEL%
test_short_s2.exe
