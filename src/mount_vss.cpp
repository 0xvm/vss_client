// mount_vss.cpp
#include <windows.h>
#include <stdio.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 3)
    {
        fwprintf(stderr, L"Usage: %s <DriveLetter:> <SnapshotDevicePath>\n", argv[0]);
        fwprintf(stderr, L"Example: %s X: \\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopy1\n", argv[0]);
        return 1;
    }

    const wchar_t* drive = argv[1];          // e.g. L"X:"
    const wchar_t* snapshotPath = argv[2];   // e.g. L"\\?\GLOBALROOT\Device\HarddiskVolumeShadowCopy1"

    // Basic sanity: drive should look like "X:"
    if (wcslen(drive) != 2 || drive[1] != L':' ||
        !((drive[0] >= L'A' && drive[0] <= L'Z') ||
          (drive[0] >= L'a' && drive[0] <= L'z')))
    {
        fwprintf(stderr, L"Invalid drive spec: %ls (expected like X:)\n", drive);
        return 1;
    }

    // Normalize to an NT path (\??\...) and append a trailing slash to target the volume root.
    std::wstring target = snapshotPath;
    if (target.rfind(L"\\\\?\\", 0) == 0)
        target.replace(0, 4, L"\\??\\"); // convert \\?\ -> \??\ (NT namespace path)
    if (!target.empty() && target.back() != L'\\')
        target.push_back(L'\\');

    // DefineDosDevice expects just "X:", no trailing backslash.
    // Use RAW_TARGET_PATH so \\?\GLOBALROOT... is not altered.
    if (!DefineDosDeviceW(
            DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM,
            drive,
            target.c_str()))
    {
        DWORD err = GetLastError();
        fwprintf(stderr, L"DefineDosDeviceW failed, error = %lu\n", err);
        return 1;
    }

    wprintf(L"Mounted %ls as %ls\n", snapshotPath, drive);
    return 0;
}
