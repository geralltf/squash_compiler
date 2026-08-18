@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
echo VCVARS_DONE
cl.exe /nologo /O2 /W0 squash.c /Fe:squash_msvc.exe
echo CL_EXIT=%ERRORLEVEL%
