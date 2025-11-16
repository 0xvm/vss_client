@echo off
setlocal

set "CRT_FLAG=/MD"
set "OUT_NAME=vss_client.exe"
if /I "%~1"=="static" (
    set "CRT_FLAG=/MT"
    set "OUT_NAME=vss_client-static.exe"
)

cl %CRT_FLAG% /O1 /GL /Gy /GR- /EHsc /W4 /I include ^
    src\vss_client.cpp ^
    src\privileges.cpp ^
    src\path_utils.cpp ^
    src\zip_writer.cpp ^
    src\snapshot_utils.cpp ^
    src\file_utils.cpp ^
    src\upload_utils.cpp ^
    /Fe:%OUT_NAME% ^
    vssapi.lib ole32.lib oleaut32.lib advapi32.lib winhttp.lib ^
    /link /MACHINE:X64 /LTCG /OPT:REF /OPT:ICF /RELEASE

if exist *.obj del *.obj
endlocal
