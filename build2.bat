@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
echo ENV_OK
cl /nologo /O2 /W0 squash.c /Fe:squash_msvc.exe
echo MSVC_EXIT=%ERRORLEVEL%
if not exist squash_msvc.exe goto fail
squash_msvc.exe squash.c -o squash2_new.exe
echo S2_EXIT=%ERRORLEVEL%
squash2_new.exe test_short_s2.c -o test_short_s2.exe
echo TP_EXIT=%ERRORLEVEL%
test_short_s2.exe
echo RUN_EXIT=%ERRORLEVEL%
goto end
:fail
echo BUILD_FAILED
:end
