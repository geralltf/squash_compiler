@echo off
cd /d C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler
.\squash.exe squash.c -o squash2.exe > build_squash2_out.txt 2>&1
echo SQUASH2_EXIT=%ERRORLEVEL% >> build_squash2_out.txt
