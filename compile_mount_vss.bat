@echo off
cl /MD /O1 /GL /Gy /GR- /EHsc /W4 ^
    src\mount_vss.cpp ^
    /Fe:mount_vss.exe ^
    advapi32.lib ^
    /link /MACHINE:X64 /LTCG /OPT:REF /OPT:ICF /RELEASE
if exist *.obj del *.obj
