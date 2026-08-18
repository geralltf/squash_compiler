@echo off
cd /d C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > /dev/null 2>&1
dumpbin /DISASM test_float_simple.exe > C:\tmp\float_disasm.txt 2>&1
echo DONE=%ERRORLEVEL%
