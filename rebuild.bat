@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
echo VCVARS_DONE
cl /nologo /O2 /W0 squash.c /Fe:squash.exe
echo CL_EXIT=%ERRORLEVEL%
