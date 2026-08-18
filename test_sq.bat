@echo off
cd /d C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler
.\squash.exe test_program2.c -o test_p2_sq.exe > sq_out.txt 2>&1
echo EXIT=%ERRORLEVEL% >> sq_out.txt
