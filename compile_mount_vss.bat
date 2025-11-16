@echo off
setlocal

set "CRT_FLAG=/MD"
set "OUT_NAME=mount_vss.exe"
if /I "%~1"=="static" (
    set "CRT_FLAG=/MT"
    set "OUT_NAME=mount_vss-static.exe"
)

cl %CRT_FLAG% /O1 /GL /Gy /GR- /EHsc /W4 ^
    src\mount_vss.cpp ^
    /Fe:%OUT_NAME% ^
    advapi32.lib ^
    /link /MACHINE:X64 /LTCG /OPT:REF /OPT:ICF /RELEASE

if exist *.obj del *.obj
endlocal
