@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
dumpbin /DISASM squash2.exe > disasm_squash2.txt 2>&1
echo DONE
