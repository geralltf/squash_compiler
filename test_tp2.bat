@echo off
cd /d C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler
.\squash2.exe test_program2.c -o test_program2_new.exe > tp2_out.txt 2>&1
echo COMPILE_EXIT=%ERRORLEVEL% >> tp2_out.txt
if exist test_program2_new.exe (
    .\test_program2_new.exe >> tp2_out.txt 2>&1
    echo RUN_EXIT=%ERRORLEVEL% >> tp2_out.txt
)
