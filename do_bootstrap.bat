@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > /dev/null 2>&1
echo VCVARS_DONE
cl /nologo /O2 /W0 squash.c /Fe:squash_msvc.exe
echo CL_EXIT=%ERRORLEVEL%
if not exist squash_msvc.exe (echo MSVC BUILD FAILED && exit /b 1)
echo Building squash2.exe...
squash_msvc.exe squash.c -o squash2.exe
echo S2_EXIT=%ERRORLEVEL%
if not exist squash2.exe (echo S2 BUILD FAILED && exit /b 1)
echo Building squash3.exe...
squash2.exe squash.c -o squash3.exe
echo S3_EXIT=%ERRORLEVEL%
echo Bootstrap done.
