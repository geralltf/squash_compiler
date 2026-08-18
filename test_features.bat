@echo off
cd /d C:\Users\Admin\OneDrive\Documents\Projects\squash_compiler
.\squash2.exe test_features.c -o test_features2.exe > test_features_out.txt 2>&1
echo COMPILE_EXIT=%ERRORLEVEL% >> test_features_out.txt
if exist test_features2.exe (
    .\test_features2.exe >> test_features_out.txt 2>&1
    echo RUN_EXIT=%ERRORLEVEL% >> test_features_out.txt
)
